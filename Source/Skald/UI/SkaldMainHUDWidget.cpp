#include "UI/SkaldMainHUDWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/Widget.h"
#include "Blueprint/UserWidget.h"
#include "Containers/Queue.h"
#include "Containers/Set.h"
#include "FighterPawn.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "CanvasItem.h"
#include "Kismet/GameplayStatics.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "SkaldTypes.h"
#include "Skald_AIController.h"
#include "Skald_GameInstance.h"
#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"

namespace {
void LogHUDWidgetCreationContext(const USkaldMainHUDWidget* Widget, const TCHAR* Callsite) {
  if (!Widget) return;
  const APlayerController* PC = Widget->GetOwningPlayer();
  const ULocalPlayer* LP = Widget->GetOwningLocalPlayer();
  UE_LOG(LogSkaldUI, Verbose, TEXT("HUDWidgetTrace[%s]: OwningPC=%s Player=%s LocalPlayer=%s"),
         Callsite, *GetNameSafe(PC), PC ? *GetNameSafe(PC->Player) : TEXT("None"), *GetNameSafe(LP));
}
}

#include "Skald_TurnManager.h"
#include "Territory.h"
#include "UI/ConfirmAttackWidget.h"
#include "UI/PrepareForBattleWidget.h"
#include "UI/DeployWidget.h"
#include "UI/CombatFloaterPoolSubsystem.h"
#include "UI/W_FloatingText.h"
#include "UI/W_DiceResolutionPanel.h"
#include "UI/SkaldPlayerListEntryWidget.h"
#include "UI/SkaldUIHelpers.h"
#include "UObject/ConstructorHelpers.h"
#include "WorldMap.h"
#include "TimerManager.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Math/UnrealMathUtility.h"

namespace SkaldSelectionPromptKeys {
static const FName ArmyPlacementOwnedTerritoryPrompt(
    TEXT("ArmyPlacementOwnedTerritoryPrompt"));
static const FName ArmyPlacementSelectOwnedTerritory(
    TEXT("ArmyPlacementSelectOwnedTerritory"));
static const FName AttackPrompt(TEXT("AttackPrompt"));
static const FName AttackSelectOwnedTerritory(TEXT("AttackSelectOwnedTerritory"));
static const FName CancelAttackArmyPlacementPrompt(
    TEXT("CancelAttackArmyPlacementPrompt"));
static const FName CancelAttackPrompt(TEXT("CancelAttackPrompt"));
static const FName CancelMovePrompt(TEXT("CancelMovePrompt"));
static const FName ChooseEnemyTerritoryPrompt(TEXT("ChooseEnemyTerritoryPrompt"));
static const FName ChooseTroopsToMovePrompt(TEXT("ChooseTroopsToMovePrompt"));
static const FName MoveDeployUICreationFailed(TEXT("MoveDeployUICreationFailed"));
static const FName MoveDeployWidgetUnavailable(TEXT("MoveDeployWidgetUnavailable"));
static const FName MoveInvalidSelection(TEXT("MoveInvalidSelection"));
static const FName MoveInvalidTarget(TEXT("MoveInvalidTarget"));
static const FName MoveNeedMoreTroops(TEXT("MoveNeedMoreTroops"));
static const FName MoveNoConnectedTerritory(TEXT("MoveNoConnectedTerritory"));
static const FName MoveNoTroopsAvailable(TEXT("MoveNoTroopsAvailable"));
static const FName MoveOwnTerritoriesOnly(TEXT("MoveOwnTerritoriesOnly"));
static const FName MoveSelectSourceTerritory(TEXT("MoveSelectSourceTerritory"));
static const FName MoveWorldMapMissing(TEXT("MoveWorldMapMissing"));
static const FName MovementPrompt(TEXT("MovementPrompt"));
static const FName ReinforcementCapitalRestriction(
    TEXT("ReinforcementCapitalRestriction"));
static const FName ReinforcementOwnedCapitalPrompt(
    TEXT("ReinforcementOwnedCapitalPrompt"));
static const FName ResetMoveSelectSource(TEXT("ResetMoveSelectSource"));
static const FName SelectConnectedTerritoryPrompt(
    TEXT("SelectConnectedTerritoryPrompt"));
} // namespace SkaldSelectionPromptKeys

USkaldMainHUDWidget::USkaldMainHUDWidget(
    const FObjectInitializer &ObjectInitializer)
    : Super(ObjectInitializer) {
  FloaterWidgetClass = UW_FloatingText::StaticClass();
  ReinforcementSelectionPromptText =
      NSLOCTEXT("SkaldHUD", "ReinforcementOwnedCapitalPrompt",
                "Select an owned capital.");
  PendingSelectionPromptText = FText::GetEmpty();

  static ConstructorHelpers::FClassFinder<UDeployWidget> DeployBP(
      TEXT("/Game/Blueprints/UI/Skald_DeployWidget"));
  if (DeployBP.Succeeded()) {
    DeployWidgetClass = DeployBP.Class;
  } else {
    UE_LOG(LogSkald, Error,
           TEXT("SkaldMainHUDWidget: failed to find deploy widget class"));
  }
  static ConstructorHelpers::FClassFinder<UConfirmAttackWidget> ConfirmBP(
      TEXT("/Game/Blueprints/UI/Skald_ConfirmAttackWidget"));
  if (ConfirmBP.Succeeded()) {
    ConfirmAttackWidgetClass = ConfirmBP.Class;
  } else {
    UE_LOG(
        LogSkald, Error,
        TEXT("SkaldMainHUDWidget: failed to find confirm attack widget class"));
  }

  static ConstructorHelpers::FClassFinder<UPrepareForBattleWidget>
      PrepareForBattleBP(
          TEXT("/Game/Blueprints/UI/Skald_PrepareForBattleWidget"));
  if (PrepareForBattleBP.Succeeded()) {
    PrepareForBattleWidgetClass = PrepareForBattleBP.Class;
  } else {
    UE_LOG(LogSkald, Warning,
           TEXT("SkaldMainHUDWidget: failed to find prepare for battle widget"));
    PrepareForBattleWidgetClass = UPrepareForBattleWidget::StaticClass();
  }
}

void USkaldMainHUDWidget::NativeConstruct() {
  Super::NativeConstruct();

  SetIsFocusable(true);
  SetFocus();

  bRetreatRequestPending = false;
  bSelectingRetreatDestination = false;
  bAwaitingRetreatConfirmation = false;
  RetreatDefendingTerritoryID = -1;
  RetreatCandidateIds.Empty();
  bHasActivePreparePrompt = false;
  bLocalPlayerIsDefender = false;
  CachedStatusMessage = FText::GetEmpty();
  CachedStatusMessageDuration = 0.f;
  bStatusMessageVisible = false;

  // Ensure the full-screen HUD doesn't swallow world clicks.
  if (UWidget *Root = GetRootWidget()) {
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
  }

  UWorld *World = GetWorld();
  if (World && World->GetNetMode() != NM_Client) {
    GameMode = World->GetAuthGameMode<ASkaldGameMode>();
    if (!GameMode) {
      UE_LOG(LogSkald, Warning,
             TEXT("SkaldMainHUDWidget could not find GameMode."));
    }
  }

  GameState = World ? World->GetGameState<ASkaldGameState>() : nullptr;
  if (!GameState) {
    UE_LOG(LogSkald, Warning,
           TEXT("SkaldMainHUDWidget could not find GameState."));
  } else {
    GameState->OnPlayersUpdated.AddDynamic(
        this, &USkaldMainHUDWidget::HandlePlayersUpdated);
    // React to replicated turn changes.
    GameState->OnTurnIndexChanged.AddDynamic(
        this, &USkaldMainHUDWidget::HandleTurnIndexChanged);
  }
  GameInstance = GetGameInstance<USkaldGameInstance>();
  if (!GameInstance) {
    UE_LOG(LogSkald, Warning,
           TEXT("SkaldMainHUDWidget could not find GameInstance."));
  }

  const int32 ResolvedLocalId = ResolveLocalPlayerId();
  if (ResolvedLocalId != -1) {
    LocalPlayerID = ResolvedLocalId;
  }

  if (AttackButton) {
    AttackButton->OnClicked.AddDynamic(
        this, &USkaldMainHUDWidget::BeginAttackSelection);
    AttackButton->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (MoveButton) {
    MoveButton->OnClicked.AddDynamic(this,
                                     &USkaldMainHUDWidget::BeginMoveSelection);
    MoveButton->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (EndTurnButton) {
    EndTurnButton->OnClicked.AddDynamic(
        this, &USkaldMainHUDWidget::HandleEndTurnClicked);
    EndTurnButton->SetVisibility(ESlateVisibility::Visible);
  }
  if (EndPhaseButton) {
    EndPhaseButton->OnClicked.AddDynamic(
        this, &USkaldMainHUDWidget::HandleEndPhaseClicked);
    EndPhaseButton->SetVisibility(ESlateVisibility::Visible);
  }
  if (DeployButton) {
    DeployButton->OnClicked.AddDynamic(
        this, &USkaldMainHUDWidget::HandleDeployClicked);
    DeployButton->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (RollInitiativeButton) {
    RollInitiativeButton->OnClicked.AddDynamic(
        this, &USkaldMainHUDWidget::HandleStrategicInitiativeRollPressed);
    RollInitiativeButton->SetVisibility(ESlateVisibility::Collapsed);
    RollInitiativeButton->SetIsEnabled(true);
  }
  if (InitiativePromptText) {
    InitiativePromptText->SetText(FText::GetEmpty());
    InitiativePromptText->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (InitiativeDiceImage) {
    InitiativeDiceImage->SetVisibility(ESlateVisibility::Collapsed);
    InitiativeDiceImage->SetBrushFromTexture(nullptr);
  }
  if (InitiativeDiceBoardImage) {
    InitiativeDiceBoardImage->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (TerritoryPanelpng) {
    TerritoryPanelpng->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (TerritoryInfoPanel) {
    TerritoryInfoPanel->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (DiceResolutionPanel) {
    DiceResolutionPanel->OnResolutionComplete.AddDynamic(
        this, &USkaldMainHUDWidget::HandleDicePanelResolved);
    DiceResolutionPanel->OnDiceOutcomeRevealed.AddDynamic(
        this, &USkaldMainHUDWidget::HandleDiceOutcomeRevealed);
    TArray<UTexture2D *> DiceFaceTexturePtrs;
    DiceFaceTexturePtrs.Reserve(DiceFaceTextures.Num());
    for (const TObjectPtr<UTexture2D> &Texture : DiceFaceTextures) {
      DiceFaceTexturePtrs.Add(Texture.Get());
    }
    DiceResolutionPanel->SetDiceFaceTextures(DiceFaceTexturePtrs);
  }

  ApplyDiceResolutionPanelLayoutInternal(DefaultDiceResolutionPanelLayout);

  ConfigureBroadcastText();

  SyncPhaseButtons(false);
  RebuildPlayerList(CachedPlayers);

  if (UCombatFloaterPoolSubsystem *FloaterPool = ResolveFloaterPool()) {
    if (FloaterWidgetClass) {
      FloaterPool->FloaterWidgetClass = FloaterWidgetClass;
    }
  }

  if (GameInstance) {
    const FText ActiveStatus = GameInstance->GetActiveStatusMessage();
    if (!ActiveStatus.IsEmpty()) {
      const bool bPersistent = GameInstance->IsActiveStatusMessagePersistent();
      const float ActiveDuration = GameInstance->GetActiveStatusMessageDuration();
      const float EffectiveDuration = bPersistent ? 0.f : ActiveDuration;
      ShowStatusMessage(ActiveStatus, EffectiveDuration);

        if (bPersistent && EffectiveDuration > KINDA_SMALL_NUMBER) {
          if (World) {
            World->GetTimerManager().ClearTimer(StatusMessageTimerHandle);
          }
        }
    }
  }
}

void USkaldMainHUDWidget::NativeTick(const FGeometry &MyGeometry,
                                     float InDeltaTime) {
  Super::NativeTick(MyGeometry, InDeltaTime);
  UpdateActiveFloaters(InDeltaTime);
}

void USkaldMainHUDWidget::NativeDestruct() {
  CompleteRetreatSelection();
  bRetreatRequestPending = false;
  bHasActivePreparePrompt = false;
  bLocalPlayerIsDefender = false;

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(StatusMessageTimerHandle);
  }

  if (DiceResolutionPanel) {
    DiceResolutionPanel->OnResolutionComplete.RemoveDynamic(
        this, &USkaldMainHUDWidget::HandleDicePanelResolved);
    DiceResolutionPanel->OnDiceOutcomeRevealed.RemoveDynamic(
        this, &USkaldMainHUDWidget::HandleDiceOutcomeRevealed);
  }

  PendingDiceResolutions.Reset();
  bDiceResolutionActive = false;
  ActiveDiceResolution = FQueuedDiceResolution();

  if (GameState) {
    GameState->OnPlayersUpdated.RemoveDynamic(
        this, &USkaldMainHUDWidget::HandlePlayersUpdated);
    GameState->OnTurnIndexChanged.RemoveDynamic(
        this, &USkaldMainHUDWidget::HandleTurnIndexChanged);
  }

  if (UWorld *World = GetWorld()) {
    FTimerManager &TimerManager = World->GetTimerManager();
    TimerManager.ClearTimer(TurnMessageTimerHandle);
    TimerManager.ClearTimer(InitiativeTimerHandle);
    TimerManager.ClearTimer(StrategicInitiativeRollDelayHandle);
    TimerManager.ClearTimer(StrategicInitiativeDiceHideHandle);
  } else {
    TurnMessageTimerHandle.Invalidate();
    InitiativeTimerHandle.Invalidate();
    StrategicInitiativeRollDelayHandle.Invalidate();
    StrategicInitiativeDiceHideHandle.Invalidate();
  }

  if (AttackButton) {
    AttackButton->OnClicked.RemoveDynamic(
        this, &USkaldMainHUDWidget::BeginAttackSelection);
  }

  if (MoveButton) {
    MoveButton->OnClicked.RemoveDynamic(
        this, &USkaldMainHUDWidget::BeginMoveSelection);
  }

  if (EndTurnButton) {
    EndTurnButton->OnClicked.RemoveDynamic(
        this, &USkaldMainHUDWidget::HandleEndTurnClicked);
  }

  if (EndPhaseButton) {
    EndPhaseButton->OnClicked.RemoveDynamic(
        this, &USkaldMainHUDWidget::HandleEndPhaseClicked);
  }

  if (DeployButton) {
    DeployButton->OnClicked.RemoveDynamic(
        this, &USkaldMainHUDWidget::HandleDeployClicked);
  }

  if (RollInitiativeButton) {
    RollInitiativeButton->OnClicked.RemoveDynamic(
        this, &USkaldMainHUDWidget::HandleStrategicInitiativeRollPressed);
  }

  while (ActiveFloaters.Num() > 0) {
    ReleaseFloaterAtIndex(ActiveFloaters.Num() - 1);
  }
  CachedFloaterPool.Reset();

  Super::NativeDestruct();
}

void USkaldMainHUDWidget::ConfigureBroadcastText() {
  if (bBroadcastTextConfigured || !EndingTurnText) {
    return;
  }

  FSlateFontInfo FontInfo = EndingTurnText->GetFont();
  const int32 OriginalSize = FontInfo.Size > 0 ? FontInfo.Size : 24;
  FontInfo.Size = OriginalSize * 2;
  EndingTurnText->SetFont(FontInfo);
  EndingTurnText->SetJustification(ETextJustify::Center);
  EndingTurnText->SetAutoWrapText(false);

  if (UCanvasPanelSlot *CanvasSlot = Cast<UCanvasPanelSlot>(EndingTurnText->Slot)) {
    CanvasSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
    CanvasSlot->SetAlignment(FVector2D(0.5f, 0.f));
    CanvasSlot->SetPosition(FVector2D(0.f, 20.f));
  } else if (UOverlaySlot *OverlaySlot = Cast<UOverlaySlot>(EndingTurnText->Slot)) {
    OverlaySlot->SetHorizontalAlignment(HAlign_Center);
    OverlaySlot->SetVerticalAlignment(VAlign_Top);
    OverlaySlot->SetPadding(FMargin(0.f, 20.f, 0.f, 0.f));
  }

  bBroadcastTextConfigured = true;
}

void USkaldMainHUDWidget::ApplyBroadcastStyle(bool bIsPlayerMessage) {
  ConfigureBroadcastText();

  if (!EndingTurnText) {
    return;
  }

  const FLinearColor PlayerColor = ResolveLocalPlayerColor();
  FLinearColor MessageColor = PlayerColor;
  if (!bIsPlayerMessage) {
    const FLinearColor EnemyColor = ResolvePlayerColor(CurrentPlayerID);
    MessageColor = EnemyColor;
  }

  EndingTurnText->SetColorAndOpacity(MessageColor);
}

const FS_PlayerData *USkaldMainHUDWidget::FindPlayerDataById(int32 PlayerId) const {
  if (PlayerId == INDEX_NONE) {
    return nullptr;
  }

  for (const FS_PlayerData &Player : CachedPlayers) {
    if (Player.PlayerID == PlayerId) {
      return &Player;
    }
  }

  return nullptr;
}

FLinearColor USkaldMainHUDWidget::ResolveFactionColor(ESkaldFaction Faction) const {
  if (GameInstance) {
    return GameInstance->GetFactionColor(Faction);
  }

  if (const UWorld *World = GetWorld()) {
    if (const USkaldGameInstance *GI =
            World->GetGameInstance<USkaldGameInstance>()) {
      return GI->GetFactionColor(Faction);
    }
  }

  return USkaldGameInstance::GetDefaultFactionColor(Faction);
}

FLinearColor USkaldMainHUDWidget::ResolvePlayerColor(int32 PlayerId) const {
  if (const FS_PlayerData *Data = FindPlayerDataById(PlayerId)) {
    return ResolveFactionColor(Data->Faction);
  }

  return ResolveFactionColor(ESkaldFaction::None);
}

FLinearColor USkaldMainHUDWidget::ResolveLocalPlayerColor() const {
  if (LocalPlayerID != -1) {
    return ResolvePlayerColor(LocalPlayerID);
  }

  if (GameInstance) {
    return ResolveFactionColor(GameInstance->Faction);
  }

  return ResolveFactionColor(ESkaldFaction::None);
}

void USkaldMainHUDWidget::HandleEndTurnClicked() {
  ShowEndingTurn();
  if (APlayerController *PC = GetOwningPlayer()) {
    if (ASkaldPlayerController *SPC = Cast<ASkaldPlayerController>(PC)) {
      SPC->EndTurn();
    }
  }
}

void USkaldMainHUDWidget::HandleEndPhaseClicked() {
  if (CurrentPhase == ETurnPhase::Attack) {
    OnEndAttackRequested.Broadcast(true);
    return;
  }

  if (CurrentPhase == ETurnPhase::Movement) {
    OnEndMovementRequested.Broadcast(true);
    return;
  }

  if (ASkaldPlayerController *PlayerController =
          Cast<ASkaldPlayerController>(GetOwningPlayer())) {
    PlayerController->EndPhase();
  }
}

void USkaldMainHUDWidget::UpdateTurnBanner(int32 InCurrentPlayerID,
                                           int32 InTurnNumber) {
  CurrentPlayerID = InCurrentPlayerID;
  TurnNumber = InTurnNumber;

  BP_SetTurnText(TurnNumber, CurrentPlayerID);
  SyncPhaseButtons(CurrentPlayerID == LocalPlayerID);
}

void USkaldMainHUDWidget::UpdatePhaseBanner(ETurnPhase InPhase) {
  ClearStrategicInitiativeWaitIfNeeded();
  CurrentPhase = InPhase;

  BP_SetPhaseText(CurrentPhase);
  if (CurrentPhase != ETurnPhase::Attack) {
    CancelAttackSelection();
  }
  if (CurrentPhase != ETurnPhase::Reinforcement &&
      CurrentPhase != ETurnPhase::ArmyPlacement) {
    ClearDeployWidget();
  }
  if (!FindFunction(TEXT("BP_SetPhaseButtons"))) {
    UE_LOG(LogSkald, Warning, TEXT("SyncPhaseButtons not bound for HUD %s"),
           *GetName());
  }
  SyncPhaseButtons(CurrentPlayerID == LocalPlayerID);

  if (DeployableUnitsText && CurrentPhase != ETurnPhase::Reinforcement &&
      CurrentPhase != ETurnPhase::ArmyPlacement) {
    DeployableUnitsText->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (!bSelectingForAttack && !bSelectingForMove) {
    if (CurrentPhase == ETurnPhase::Reinforcement) {
      const FText Prompt = ResolveSelectionPromptText(
          SkaldSelectionPromptKeys::ReinforcementOwnedCapitalPrompt,
          ReinforcementSelectionPromptText);
      ShowSelectionPromptMessage(Prompt);
    } else if (CurrentPhase == ETurnPhase::ArmyPlacement) {
      const FText Prompt = ResolveSelectionPromptText(
          SkaldSelectionPromptKeys::ArmyPlacementOwnedTerritoryPrompt,
          NSLOCTEXT("SkaldHUD", "ArmyPlacementOwnedTerritoryPrompt",
                    "Select an owned territory. You may deploy up to 10 troops per territory."));
      ShowSelectionPromptMessage(Prompt);
    } else if (CurrentPhase == ETurnPhase::Movement) {
      const FText Prompt = ResolveSelectionPromptText(
          SkaldSelectionPromptKeys::MovementPrompt,
          NSLOCTEXT("SkaldHUD", "MovementPrompt",
                    "Press Move, then select an owned territory."));
      ShowSelectionPromptMessage(Prompt);
    } else if (CurrentPhase == ETurnPhase::Attack) {
      const FText Prompt = ResolveSelectionPromptText(
          SkaldSelectionPromptKeys::AttackPrompt,
          NSLOCTEXT("SkaldHUD", "AttackPrompt",
                    "Press Attack, then select an owned territory."));
      ShowSelectionPromptMessage(Prompt);
    } else {
      ShowSelectionPromptMessage(FText::GetEmpty(), false);
    }
  }
}

void USkaldMainHUDWidget::UpdateTerritoryInfo(const FString &TerritoryName,
                                              const FString &OwnerName,
                                              int32 ArmyCount) {
  BP_SetTerritoryPanel(TerritoryName, OwnerName, ArmyCount);

  if (TerritoryPanelpng) {
    TerritoryPanelpng->SetVisibility(ESlateVisibility::Visible);
  }

  if (TerritoryInfoPanel) {
    TerritoryInfoPanel->SetVisibility(ESlateVisibility::Visible);
  }

  // Keep Deploy button visibility in sync with current selection ownership.
  if (LocalPlayerID == -1) {
    const int32 ResolvedLocalId = ResolveLocalPlayerId();
    if (ResolvedLocalId != -1) {
      LocalPlayerID = ResolvedLocalId;
    }
  }
  const bool bIsMyTurn = (CurrentPlayerID != -1 && LocalPlayerID != -1 &&
                          CurrentPlayerID == LocalPlayerID);

  if (DeployButton && (CurrentPhase == ETurnPhase::Reinforcement ||
                       CurrentPhase == ETurnPhase::ArmyPlacement)) {
    bool bOwnedByLocal = false;
    bool bIsCapital = false;
    if (APlayerController *PC = GetOwningPlayer()) {
      if (ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>()) {
        if (ATerritory *Sel = PS->SelectedTerritory.Get()) {
          bOwnedByLocal =
              (Sel->OwningPlayer &&
               Sel->OwningPlayer->GetStablePlayerId() ==
                   PS->GetStablePlayerId());
          if (bOwnedByLocal) {
            bIsCapital = Sel->bIsCapital;
          }
        }
      }
    }
    const bool bIsArmyPlacement = CurrentPhase == ETurnPhase::ArmyPlacement;
    const bool bCanDeployHere =
        bOwnedByLocal && (bIsArmyPlacement || bIsCapital);
    const bool bShouldShowDeploy = bIsMyTurn && bCanDeployHere;
    DeployButton->SetVisibility(bShouldShowDeploy ? ESlateVisibility::Visible
                                                  : ESlateVisibility::Collapsed);
    DeployButton->SetIsEnabled(bShouldShowDeploy);
  }
}

void USkaldMainHUDWidget::ClearTerritoryInfo() {
  BP_ClearTerritoryPanel();

  if (TerritoryPanelpng) {
    TerritoryPanelpng->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (TerritoryInfoPanel) {
    TerritoryInfoPanel->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (DeployButton) {
    DeployButton->SetVisibility(ESlateVisibility::Collapsed);
    DeployButton->SetIsEnabled(false);
  }
}

void USkaldMainHUDWidget::RefreshPlayerList(
    const TArray<FS_PlayerData> &Players) {
  CachedPlayers = Players;
  RebuildPlayerList(CachedPlayers);
}

void USkaldMainHUDWidget::RefreshFromState(
    int32 InCurrentPlayerID, int32 InTurnNumber, ETurnPhase InPhase,
    const TArray<FS_PlayerData> &Players) {
  CurrentPlayerID = InCurrentPlayerID;
  TurnNumber = InTurnNumber;
  CurrentPhase = InPhase;
  ClearStrategicInitiativeWaitIfNeeded();
  CachedPlayers = Players;

  BP_SetTurnText(TurnNumber, CurrentPlayerID);
  BP_SetPhaseText(CurrentPhase);
  RebuildPlayerList(CachedPlayers);
  SyncPhaseButtons(CurrentPlayerID == LocalPlayerID);
  SetSelectionPromptSuppressed(CurrentPlayerID != LocalPlayerID);
}

void USkaldMainHUDWidget::ShowTurnAnnouncement(const FString &PlayerName) {
  BP_ShowTurnAnnouncement(PlayerName);
  if (GEngine) {
    const FString Message = FString::Printf(TEXT("%s's Turn"), *PlayerName);
    GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, Message);
  }
}

void USkaldMainHUDWidget::RebuildPlayerList(
    const TArray<FS_PlayerData> &Players) {
  if (!PlayerListBox) {
    return;
  }

  if (const UWorld *World = GetWorld()) {
    // Avoid creating widgets while the world is tearing down (e.g. during
    // travel), which would trigger an ensure in CreateWidget.
    if (World->bIsTearingDown) {
      UE_LOG(LogSkaldUI, Verbose,
             TEXT("[HUD] Skipping player list rebuild because world is tearing down"));
      return;
    }
  }

  PlayerListBox->ClearChildren();
  ULocalPlayer *OwningLocalPlayer = GetOwningLocalPlayer();

  UEnum *FactionEnum = StaticEnum<ESkaldFaction>();
  const bool bUseCustomEntries = PlayerListEntryWidgetClass != nullptr;

  for (const FS_PlayerData &Player : Players) {
    if (Player.IsEliminated) {
      continue;
    }

    const FString FactionName = FactionEnum
                                    ? FactionEnum
                                          ->GetDisplayNameTextByValue(
                                              static_cast<int64>(Player.Faction))
                                          .ToString()
                                    : TEXT("Unknown");
    const int32 TerritoryCount = Player.TerritoriesOwned;

    if (bUseCustomEntries) {
      if (!OwningLocalPlayer) {
        UE_LOG(LogSkaldUI, Verbose,
               TEXT("[HUD] Skipping custom player list entries due to missing owning local player."));
        break;
      }
      LogHUDWidgetCreationContext(this, TEXT("RebuildPlayerList.Entry"));
      USkaldPlayerListEntryWidget *EntryWidget =
          CreateWidget<USkaldPlayerListEntryWidget>(GetOwningPlayer(),
                                                    PlayerListEntryWidgetClass);
      if (!EntryWidget) {
        continue;
      }

      EntryWidget->SetupPlayerEntry(Player, TerritoryCount);
      PlayerListBox->AddChild(EntryWidget);
      continue;
    }

    UTextBlock *Entry = NewObject<UTextBlock>(PlayerListBox);
    if (!Entry) {
      continue;
    }

    FString Line = FString::Printf(TEXT("%s (%s) - Territories: %d%s"),
                                   *Player.PlayerName, *FactionName,
                                   TerritoryCount,
                                   Player.IsAI ? TEXT(" [AI]") : TEXT(""));
    Entry->SetText(FText::FromString(Line));
    PlayerListBox->AddChildToVerticalBox(Entry);
  }
}

void USkaldMainHUDWidget::ShowEndingTurn() {
  if (EndingTurnText) {
    ApplyBroadcastStyle(true);
    EndingTurnText->SetText(FText::FromString(TEXT("Ending turn.")));
    EndingTurnText->SetVisibility(ESlateVisibility::Visible);
    if (UWorld *World = GetWorld()) {
      FTimerManager &TimerManager = World->GetTimerManager();
      TimerManager.ClearTimer(TurnMessageTimerHandle);

      const TWeakObjectPtr<USkaldMainHUDWidget> WeakThis(this);
      FTimerDelegate TimerDelegate;
      TimerDelegate.BindLambda([WeakThis]() {
        if (WeakThis.IsValid()) {
          WeakThis->HideEndingTurn();
        }
      });

      TimerManager.SetTimer(TurnMessageTimerHandle, TimerDelegate, 3.f, false);
    }
  }
}

void USkaldMainHUDWidget::HideEndingTurn() {
  if (EndingTurnText) {
    EndingTurnText->SetVisibility(ESlateVisibility::Collapsed);
  }
}

void USkaldMainHUDWidget::ShowTurnMessage(bool bIsMyTurn) {
  if (EndingTurnText) {
    ApplyBroadcastStyle(bIsMyTurn);
    EndingTurnText->SetText(
        FText::FromString(bIsMyTurn ? TEXT("Your turn") : TEXT("Enemy turn")));
    EndingTurnText->SetVisibility(ESlateVisibility::Visible);
  }
  SyncPhaseButtons(bIsMyTurn);
  SetSelectionPromptSuppressed(!bIsMyTurn);
  if (UWorld *World = GetWorld()) {
    FTimerManager &TimerManager = World->GetTimerManager();
    TimerManager.ClearTimer(TurnMessageTimerHandle);

    const TWeakObjectPtr<USkaldMainHUDWidget> WeakThis(this);
    FTimerDelegate TimerDelegate;
    TimerDelegate.BindLambda([WeakThis]() {
      if (WeakThis.IsValid()) {
        WeakThis->HideEndingTurn();
      }
    });

    TimerManager.SetTimer(TurnMessageTimerHandle, TimerDelegate, 3.f, false);
  }
}

void USkaldMainHUDWidget::ShowEnemyTurnInProgress(const FString &Message) {
  if (EndingTurnText) {
    ApplyBroadcastStyle(false);
    EndingTurnText->SetText(FText::FromString(Message));
    EndingTurnText->SetVisibility(ESlateVisibility::Visible);
  }
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(TurnMessageTimerHandle);
  }
  SyncPhaseButtons(false);
  SetSelectionPromptSuppressed(true);
}

void USkaldMainHUDWidget::HideEnemyTurnInProgress() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(TurnMessageTimerHandle);
  }
  HideEndingTurn();
  SetSelectionPromptSuppressed(!IsLocalPlayersTurn());
}

void USkaldMainHUDWidget::ShowStrategicInitiativePrompt(const FText &PromptText,
                                                        float ButtonDelay) {
  if (InitiativePromptText) {
    InitiativePromptText->SetText(PromptText);
    InitiativePromptText->SetVisibility(ESlateVisibility::HitTestInvisible);
  }

  if (RollInitiativeButton) {
    RollInitiativeButton->SetVisibility(ESlateVisibility::Collapsed);
    RollInitiativeButton->SetIsEnabled(false);
  }

  if (UWorld *World = GetWorld()) {
    FTimerManager &TimerManager = World->GetTimerManager();
    TimerManager.ClearTimer(StrategicInitiativeRollDelayHandle);

    if (RollInitiativeButton) {
      const float DelaySeconds = FMath::Max(ButtonDelay, 0.f);
      if (DelaySeconds <= KINDA_SMALL_NUMBER) {
        RevealStrategicInitiativeRollButton();
      } else {
        TimerManager.SetTimer(StrategicInitiativeRollDelayHandle, this,
                              &USkaldMainHUDWidget::RevealStrategicInitiativeRollButton,
                              DelaySeconds, false);
      }
    }
  }

  SetAwaitingStrategicInitiative(true);
}

void USkaldMainHUDWidget::HideStrategicInitiativePrompt() {
  if (InitiativePromptText) {
    InitiativePromptText->SetText(FText::GetEmpty());
    InitiativePromptText->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (RollInitiativeButton) {
    RollInitiativeButton->SetVisibility(ESlateVisibility::Collapsed);
    RollInitiativeButton->SetIsEnabled(true);
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(StrategicInitiativeRollDelayHandle);
  } else {
    StrategicInitiativeRollDelayHandle.Invalidate();
  }
}

void USkaldMainHUDWidget::ShowPrepareForBattleDialog(
    const FPrepareForBattlePromptData &PromptData) {
  bPrepareForBattleReadySent = false;
  bRetreatRequestPending = false;
  bSelectingRetreatDestination = false;
  bAwaitingRetreatConfirmation = false;
  RetreatCandidateIds.Empty();
  RetreatDefendingTerritoryID = -1;
  ActivePreparePrompt = PromptData;
  bHasActivePreparePrompt = true;
  bLocalPlayerIsDefender = false;

  if (ActivePrepareForBattleWidget) {
    ActivePrepareForBattleWidget->OnPrepareButtonClicked.RemoveAll(this);
    ActivePrepareForBattleWidget->OnRetreatButtonClicked.RemoveAll(this);
    ActivePrepareForBattleWidget->RemoveFromParent();
    ActivePrepareForBattleWidget = nullptr;
  }

  if (!PrepareForBattleWidgetClass) {
    UE_LOG(LogSkald, Warning,
           TEXT("ShowPrepareForBattleDialog: PrepareForBattleWidgetClass null"));
    return;
  }
  if (!GetOwningLocalPlayer()) {
    UE_LOG(LogSkald, Warning,
           TEXT("ShowPrepareForBattleDialog: missing owning local player"));
    return;
  }

  LogHUDWidgetCreationContext(this, TEXT("ShowPrepareForBattleDialog"));
  ActivePrepareForBattleWidget = CreateWidget<UPrepareForBattleWidget>(
      GetOwningPlayer(), PrepareForBattleWidgetClass);
  if (!ActivePrepareForBattleWidget) {
    UE_LOG(LogSkald, Warning,
           TEXT("ShowPrepareForBattleDialog: failed to create widget"));
    return;
  }

  int32 EffectiveLocalId = LocalPlayerID;
  bool bLocalIsAttacker = false;
  bool bLocalIsDefender = false;
  if (APlayerController *OwnerPC = GetOwningPlayer()) {
    if (ASkaldPlayerState *LocalPS = OwnerPC->GetPlayerState<ASkaldPlayerState>()) {
      const int32 PlayerId = LocalPS->GetStablePlayerId();
      if (EffectiveLocalId <= 0 && PlayerId > 0) {
        EffectiveLocalId = PlayerId;
      }
    }
  }

  if (EffectiveLocalId > 0) {
    bLocalIsAttacker = EffectiveLocalId == PromptData.AttackerPlayerID;
    bLocalIsDefender = EffectiveLocalId == PromptData.DefenderPlayerID;
  }

  bLocalPlayerIsDefender = bLocalIsDefender;

  auto BuildPlayerDisplayText = [&](const FText &Preferred, int32 PlayerId,
                                    const TCHAR *LogContext) -> FText {
    if (!Preferred.IsEmptyOrWhitespace()) {
      return Preferred;
    }

    if (PlayerId > 0) {
      UE_LOG(LogSkald, Verbose,
             TEXT("ShowPrepareForBattleDialog: falling back to player ID for %s (ID %d)"),
             LogContext, PlayerId);
      return FText::Format(
          NSLOCTEXT("SkaldHUD", "Prepare_PlayerIDFallback", "Player #{0}"),
          FText::AsNumber(PlayerId));
    }

    UE_LOG(LogSkald, Warning,
           TEXT("ShowPrepareForBattleDialog: missing player name and ID for %s"),
           LogContext);
    return NSLOCTEXT("SkaldHUD", "Prepare_UnknownPlayer", "Unknown Player");
  };

  auto BuildTerritoryDisplayText = [&](const FText &Preferred, int32 TerritoryId,
                                       const TCHAR *LogContext) -> FText {
    if (!Preferred.IsEmptyOrWhitespace()) {
      return Preferred;
    }

    if (TerritoryId != 0) {
      UE_LOG(LogSkald, Verbose,
             TEXT("ShowPrepareForBattleDialog: falling back to territory ID for %s (ID %d)"),
             LogContext, TerritoryId);
      return FText::Format(NSLOCTEXT("SkaldHUD", "Prepare_TerritoryIDFallback",
                                     "Territory #{0}"),
                           FText::AsNumber(TerritoryId));
    }

    UE_LOG(LogSkald, Warning,
           TEXT("ShowPrepareForBattleDialog: missing territory data for %s"),
           LogContext);
    return NSLOCTEXT("SkaldHUD", "Prepare_UnknownTerritory",
                     "Unknown Territory");
  };

  auto ResolveFactionTexture = [&](const TSoftObjectPtr<UTexture2D> &Source,
                                   ESkaldFaction Faction,
                                   const TCHAR *LogContext) -> UTexture2D * {
    TSoftObjectPtr<UTexture2D> Candidate = Source;
    if (!Candidate.ToSoftObjectPath().IsValid() && GameInstance &&
        Faction != ESkaldFaction::None) {
      Candidate = GameInstance->GetFactionEmblem(Faction);
    }

    if (!Candidate.ToSoftObjectPath().IsValid()) {
      if (Faction != ESkaldFaction::None) {
        UE_LOG(LogSkald, Verbose,
               TEXT("ShowPrepareForBattleDialog: no faction emblem path for %s (%s)"),
               LogContext,
               *StaticEnum<ESkaldFaction>()->GetNameStringByValue(
                   static_cast<int64>(Faction)));
      }
      return nullptr;
    }

    if (UTexture2D *Existing = Candidate.Get()) {
      return Existing;
    }

    UTexture2D *Loaded = Candidate.LoadSynchronous();
    if (!Loaded) {
      UE_LOG(LogSkald, Warning,
             TEXT("ShowPrepareForBattleDialog: failed to load faction emblem for %s from path %s"),
             LogContext, *Candidate.ToSoftObjectPath().ToString());
    }
    return Loaded;
  };

  const FText AttackerPlayerText = BuildPlayerDisplayText(
      PromptData.AttackerDisplayName, PromptData.AttackerPlayerID,
      TEXT("Attacker"));
  const FText DefenderPlayerText = BuildPlayerDisplayText(
      PromptData.DefenderDisplayName, PromptData.DefenderPlayerID,
      TEXT("Defender"));
  const FText AttackerTerritoryText = BuildTerritoryDisplayText(
      PromptData.AttackingTerritoryName, PromptData.AttackingTerritoryID,
      TEXT("AttackingTerritory"));
  const FText DefenderTerritoryText = BuildTerritoryDisplayText(
      PromptData.DefendingTerritoryName, PromptData.DefendingTerritoryID,
      TEXT("DefendingTerritory"));

  UTexture2D *AttackerEmblem = ResolveFactionTexture(
      PromptData.AttackerFactionEmblem, PromptData.AttackerFaction,
      TEXT("Attacker"));
  UTexture2D *DefenderEmblem = ResolveFactionTexture(
      PromptData.DefenderFactionEmblem, PromptData.DefenderFaction,
      TEXT("Defender"));

  ActivePrepareForBattleWidget->SetupBattleDetails(
      AttackerPlayerText, AttackerTerritoryText, AttackerEmblem,
      DefenderPlayerText, DefenderTerritoryText, DefenderEmblem,
      PromptData.AttackerCommittedArmy, PromptData.DefenderArmyCount);
  const FLinearColor AttackerColor =
      ResolveFactionColor(PromptData.AttackerFaction);
  const FLinearColor DefenderColor =
      ResolveFactionColor(PromptData.DefenderFaction);
  ActivePrepareForBattleWidget->SetFactionColors(AttackerColor, DefenderColor);
  ActivePrepareForBattleWidget->ShowRetreatStatus(FText::GetEmpty(), 0.f);
  if (ActivePrepareForBattleWidget->PrepareForBattleButton) {
    const bool bLocalIsParticipant = bLocalIsAttacker || bLocalIsDefender;
    const ESlateVisibility PrepareVisibility =
        bLocalIsParticipant ? ESlateVisibility::Visible
                             : ESlateVisibility::Collapsed;
    ActivePrepareForBattleWidget->PrepareForBattleButton->SetVisibility(
        PrepareVisibility);
    ActivePrepareForBattleWidget->PrepareForBattleButton->SetIsEnabled(
        bLocalIsParticipant);
  }
  ActivePrepareForBattleWidget->SetRetreatButtonVisibility(
      bLocalIsDefender ? ESlateVisibility::Visible
                        : ESlateVisibility::Collapsed);
  if (bLocalIsDefender) {
    ActivePrepareForBattleWidget->OnRetreatButtonClicked.AddDynamic(
        this, &USkaldMainHUDWidget::HandleRetreatClicked);
    if (ActivePrepareForBattleWidget->RetreatButton) {
      ActivePrepareForBattleWidget->RetreatButton->SetIsEnabled(true);
    }
  }

  ActivePrepareForBattleWidget->SetVisibility(ESlateVisibility::Visible);
  ActivePrepareForBattleWidget->SetRenderOpacity(1.f);
  ActivePrepareForBattleWidget->SetIsEnabled(true);
  ActivePrepareForBattleWidget->OnPrepareButtonClicked.AddDynamic(
      this, &USkaldMainHUDWidget::HandlePrepareForBattleClicked);
  ActivePrepareForBattleWidget->AddToViewport(20);
}

void USkaldMainHUDWidget::HidePrepareForBattleDialog() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(RetreatPromptTeardownHandle);
  }

  if (ActivePrepareForBattleWidget) {
    ActivePrepareForBattleWidget->OnPrepareButtonClicked.RemoveAll(this);
    ActivePrepareForBattleWidget->OnRetreatButtonClicked.RemoveAll(this);
    ActivePrepareForBattleWidget->ShowRetreatStatus(FText::GetEmpty(), 0.f);
    ActivePrepareForBattleWidget->RemoveFromParent();
    ActivePrepareForBattleWidget = nullptr;
  }
  bPrepareForBattleReadySent = false;
  bRetreatRequestPending = false;
  bHasActivePreparePrompt = false;
  bLocalPlayerIsDefender = false;
}

void USkaldMainHUDWidget::BeginRetreatSelection(
    int32 DefendingTerritoryID, const TArray<int32> &CandidateTerritoryIDs) {
  HidePrepareForBattleDialog();

  bSelectingRetreatDestination = true;
  bAwaitingRetreatConfirmation = false;
  bRetreatRequestPending = false;
  RetreatDefendingTerritoryID = DefendingTerritoryID;
  RetreatCandidateIds.Empty();
  for (int32 CandidateID : CandidateTerritoryIDs) {
    RetreatCandidateIds.Add(CandidateID);
  }

  bSelectingForAttack = false;
  bSelectingForMove = false;
  SelectedSourceID = -1;
  SelectedTargetID = -1;

  ClearTerritoryHighlights();

  const FText PromptText = NSLOCTEXT(
      "SkaldHUD", "RetreatSelectPrompt",
      "Select a highlighted territory to retreat to.");
  ShowSelectionPromptMessage(PromptText);

  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    for (int32 CandidateID : RetreatCandidateIds) {
      if (ATerritory *Candidate = WorldMap->GetTerritoryById(CandidateID)) {
        Candidate->Select(LocalPlayerID);
        HighlightedTerritories.Add(Candidate);
      }
    }
  }
}

void USkaldMainHUDWidget::CompleteRetreatSelection() {
  if (!bSelectingRetreatDestination && !bAwaitingRetreatConfirmation) {
    RetreatCandidateIds.Empty();
    RetreatDefendingTerritoryID = -1;
    return;
  }

  bSelectingRetreatDestination = false;
  bAwaitingRetreatConfirmation = false;
  RetreatCandidateIds.Empty();
  RetreatDefendingTerritoryID = -1;
  ClearTerritoryHighlights();
  ShowSelectionPromptMessage(FText::GetEmpty(), false);
}

void USkaldMainHUDWidget::ShowRetreatUnavailableMessage(const FText &Message) {
  bRetreatRequestPending = false;
  bAwaitingRetreatConfirmation = false;

  if (ActivePrepareForBattleWidget) {
    ActivePrepareForBattleWidget->ShowRetreatStatus(Message, 2.f);
    if (ActivePrepareForBattleWidget->RetreatButton) {
      ActivePrepareForBattleWidget->RetreatButton->SetIsEnabled(true);
    }
  } else {
    ShowErrorMessage(Message.ToString());
  }
}

bool USkaldMainHUDWidget::ShowEnemyRetreatedMessage() {
  bool bDisplayedRetreatStatus = false;

  if (ActivePrepareForBattleWidget) {
    const FText RetreatMessage = NSLOCTEXT(
        "SkaldHUD", "PrepareEnemyRetreatedStatus",
        "Enemy retreated. Returning to map...");
    ActivePrepareForBattleWidget->ShowRetreatStatus(RetreatMessage, 0.f);

    ActivePrepareForBattleWidget->SetRetreatButtonVisibility(
        ESlateVisibility::Collapsed);

    if (ActivePrepareForBattleWidget->PrepareForBattleButton) {
      ActivePrepareForBattleWidget->PrepareForBattleButton->SetVisibility(
          ESlateVisibility::Collapsed);
      ActivePrepareForBattleWidget->PrepareForBattleButton->SetIsEnabled(false);
    }

    ActivePrepareForBattleWidget->SetIsEnabled(false);
    bDisplayedRetreatStatus = true;

    if (UWorld *World = GetWorld()) {
      FTimerManager &TimerManager = World->GetTimerManager();
      TimerManager.ClearTimer(RetreatPromptTeardownHandle);

      FTimerDelegate TeardownDelegate;
      TeardownDelegate.BindUObject(this,
                                   &USkaldMainHUDWidget::HidePrepareForBattleDialog);
      TimerManager.SetTimer(RetreatPromptTeardownHandle, TeardownDelegate, 2.f,
                            false);
    } else {
      HidePrepareForBattleDialog();
    }
  }

  if (!EndingTurnText) {
    return bDisplayedRetreatStatus;
  }

  ApplyBroadcastStyle(true);
  EndingTurnText->SetText(
      NSLOCTEXT("SkaldHUD", "EnemyRetreatedMessage", "Enemy Retreated"));
  EndingTurnText->SetVisibility(ESlateVisibility::Visible);

  if (UWorld *World = GetWorld()) {
    FTimerManager &TimerManager = World->GetTimerManager();
    TimerManager.ClearTimer(TurnMessageTimerHandle);
    TimerManager.ClearTimer(RetreatPromptTeardownHandle);

    const TWeakObjectPtr<USkaldMainHUDWidget> WeakThis(this);
    FTimerDelegate TimerDelegate;
    TimerDelegate.BindLambda([WeakThis]() {
      if (WeakThis.IsValid()) {
        WeakThis->HideEndingTurn();
      }
    });

    TimerManager.SetTimer(TurnMessageTimerHandle, TimerDelegate, 2.f, false);
  }

  return bDisplayedRetreatStatus;
}

void USkaldMainHUDWidget::HandleRetreatClicked() {
  if (bRetreatRequestPending || !bLocalPlayerIsDefender || !bHasActivePreparePrompt) {
    return;
  }

  bRetreatRequestPending = true;
  bAwaitingRetreatConfirmation = false;

  if (ActivePrepareForBattleWidget) {
    ActivePrepareForBattleWidget->ShowRetreatStatus(FText::GetEmpty(), 0.f);
    if (ActivePrepareForBattleWidget->RetreatButton) {
      ActivePrepareForBattleWidget->RetreatButton->SetIsEnabled(false);
    }
  }

  OnRetreatRequested.Broadcast();
}

void USkaldMainHUDWidget::ShowStrategicInitiativeRoll(int32 RollValue,
                                                      float DisplayDuration) {
  if (!InitiativeDiceImage) {
    return;
  }

  if (RollValue <= 0) {
    HideStrategicInitiativeDice();
    return;
  }

  UObject *DiceResource = nullptr;
  const int32 TextureIndex = RollValue - 1;
  if (DiceFaceTextures.IsValidIndex(TextureIndex)) {
    DiceResource = DiceFaceTextures[TextureIndex].Get();
  }

  if (!DiceResource) {
    if (!StrategicInitiativeDiceRenderTarget) {
      StrategicInitiativeDiceRenderTarget =
          UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(
              this, UCanvasRenderTarget2D::StaticClass(), 256, 256);
      if (StrategicInitiativeDiceRenderTarget) {
        StrategicInitiativeDiceRenderTarget->OnCanvasRenderTargetUpdate.AddDynamic(
            this, &USkaldMainHUDWidget::HandleStrategicDiceRenderTargetUpdate);
      }
    }

    if (StrategicInitiativeDiceRenderTarget) {
      PendingStrategicInitiativeValue = RollValue;
      StrategicInitiativeDiceRenderTarget->ClearColor = FLinearColor::Transparent;
      StrategicInitiativeDiceRenderTarget->UpdateResourceImmediate();
      DiceResource = StrategicInitiativeDiceRenderTarget;
    }
  }

  const float DiceDisplaySize = 180.f;
  const float DiceBoardPadding = 24.f;
  const FVector2D DiceOffset = FVector2D::ZeroVector;

  bool bDisplayedDice = false;
  if (DiceResource) {
    if (UTexture2D *Texture = Cast<UTexture2D>(DiceResource)) {
      InitiativeDiceImage->SetBrushFromTexture(Texture, true);
    } else {
      FSlateBrush Brush = InitiativeDiceImage->GetBrush();
      Brush.SetResourceObject(DiceResource);
      Brush.ImageSize = FVector2D(DiceDisplaySize, DiceDisplaySize);
      InitiativeDiceImage->SetBrush(Brush);
    }

    InitiativeDiceImage->SetDesiredSizeOverride(
        FVector2D(DiceDisplaySize, DiceDisplaySize));
    InitiativeDiceImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

    if (UCanvasPanelSlot *DiceSlot =
            Cast<UCanvasPanelSlot>(InitiativeDiceImage->Slot)) {
      DiceSlot->SetAnchors(FAnchors(0.5f, 0.5f));
      DiceSlot->SetAlignment(FVector2D(0.5f, 0.5f));
      DiceSlot->SetPosition(DiceOffset);
      DiceSlot->SetSize(FVector2D(DiceDisplaySize, DiceDisplaySize));
    }

    InitiativeDiceImage->SetVisibility(ESlateVisibility::HitTestInvisible);

    if (InitiativeDiceBoardImage) {
      InitiativeDiceBoardImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

      if (UCanvasPanelSlot *BoardSlot =
              Cast<UCanvasPanelSlot>(InitiativeDiceBoardImage->Slot)) {
        BoardSlot->SetAnchors(FAnchors(0.5f, 0.5f));
        BoardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
        BoardSlot->SetPosition(DiceOffset);
        BoardSlot->SetSize(FVector2D(DiceDisplaySize + DiceBoardPadding,
                                     DiceDisplaySize + DiceBoardPadding));
      }

      InitiativeDiceBoardImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    }

    bDisplayedDice = true;
  } else {
    HideStrategicInitiativeDice();
  }

  if (bDisplayedDice && InitiativeDiceSound) {
    UGameplayStatics::PlaySound2D(this, InitiativeDiceSound);
  }

  if (bDisplayedDice) {
    if (UWorld *World = GetWorld()) {
      FTimerManager &TimerManager = World->GetTimerManager();
      TimerManager.ClearTimer(StrategicInitiativeDiceHideHandle);

      const float ClampedDuration =
          FMath::Clamp(DisplayDuration, 0.35f, 1.25f);
      TimerManager.SetTimer(StrategicInitiativeDiceHideHandle, this,
                            &USkaldMainHUDWidget::HideStrategicInitiativeDice,
                            ClampedDuration, false);
    }
  }
}

void USkaldMainHUDWidget::HandleStrategicInitiativeRollPressed() {
  if (RollInitiativeButton) {
    RollInitiativeButton->SetIsEnabled(false);
  }

  HideStrategicInitiativePrompt();

  OnStrategicInitiativeRollRequested.Broadcast();
}

void USkaldMainHUDWidget::RevealStrategicInitiativeRollButton() {
  if (RollInitiativeButton) {
    RollInitiativeButton->SetVisibility(ESlateVisibility::Visible);
    RollInitiativeButton->SetIsEnabled(true);
  }
}

void USkaldMainHUDWidget::SetAwaitingStrategicInitiative(bool bAwaiting) {
  if (bAwaitingStrategicInitiative == bAwaiting) {
    return;
  }

  bAwaitingStrategicInitiative = bAwaiting;

  if (bAwaiting) {
    if (UTextBlock *PromptLabel = GetSelectionPromptTextBlock()) {
      PromptLabel->SetText(FText::GetEmpty());
      PromptLabel->SetVisibility(ESlateVisibility::Collapsed);
    }
  } else {
    ApplyPendingSelectionPrompt();
  }

  const bool bIsMyTurn = CurrentPlayerID == LocalPlayerID;
  SyncPhaseButtons(bIsMyTurn);
}

void USkaldMainHUDWidget::ClearStrategicInitiativeWaitIfNeeded() {
  if (!bAwaitingStrategicInitiative) {
    return;
  }

  if (IsStrategicInitiativeOverlayActive()) {
    return;
  }

  SetAwaitingStrategicInitiative(false);
}

bool USkaldMainHUDWidget::IsStrategicInitiativeOverlayActive() const {
  const auto IsWidgetVisible = [](const UWidget *Widget) {
    if (!Widget) {
      return false;
    }
    const ESlateVisibility Visibility = Widget->GetVisibility();
    return Visibility != ESlateVisibility::Collapsed &&
           Visibility != ESlateVisibility::Hidden;
  };

  if (IsWidgetVisible(InitiativePromptText)) {
    return true;
  }
  if (IsWidgetVisible(RollInitiativeButton)) {
    return true;
  }
  if (IsWidgetVisible(InitiativeDiceImage)) {
    return true;
  }
  if (IsWidgetVisible(InitiativeDiceBoardImage)) {
    return true;
  }

  return false;
}

void USkaldMainHUDWidget::HideStrategicInitiativeDice() {
  if (!InitiativeDiceImage) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(StrategicInitiativeDiceHideHandle);
  } else {
    StrategicInitiativeDiceHideHandle.Invalidate();
  }

  InitiativeDiceImage->SetVisibility(ESlateVisibility::Collapsed);
  InitiativeDiceImage->SetBrushFromTexture(nullptr);
  PendingStrategicInitiativeValue = 0;

  if (InitiativeDiceBoardImage) {
    InitiativeDiceBoardImage->SetVisibility(ESlateVisibility::Collapsed);
  }

  SetAwaitingStrategicInitiative(false);
}

void USkaldMainHUDWidget::HandleStrategicDiceRenderTargetUpdate(
    UCanvas *Canvas, int32 Width, int32 Height) {
  if (!Canvas || Width <= 0 || Height <= 0) {
    return;
  }

  const FVector2D Size(Width, Height);
  const FLinearColor BackgroundColor(0.f, 0.f, 0.f, 0.75f);
  FCanvasTileItem Tile(FVector2D::ZeroVector, Size, BackgroundColor);
  Tile.BlendMode = SE_BLEND_Translucent;
  Canvas->DrawItem(Tile);

  const FLinearColor BorderColor(1.f, 1.f, 1.f, 0.85f);
  const float BorderThickness = 6.f;
  FCanvasBoxItem Border(FVector2D::ZeroVector, Size);
  Border.SetColor(BorderColor);
  Border.LineThickness = BorderThickness;
  Canvas->DrawItem(Border);

  if (!GEngine || PendingStrategicInitiativeValue <= 0) {
    return;
  }

  UFont *Font = GEngine->GetLargeFont();
  if (!Font) {
    Font = GEngine->GetMediumFont();
  }
  if (!Font) {
    Font = GEngine->GetSmallFont();
  }
  if (!Font) {
    return;
  }

  const FString RollString = FString::FromInt(PendingStrategicInitiativeValue);
  FCanvasTextItem TextItem(FVector2D::ZeroVector, FText::FromString(RollString),
                           Font, FLinearColor::White);
  TextItem.bCentreX = true;
  TextItem.bCentreY = true;
  TextItem.EnableShadow(FLinearColor::Black);
  TextItem.Scale = FVector2D(2.6f, 2.6f);
  Canvas->DrawItem(TextItem);
}

void USkaldMainHUDWidget::UpdateInitiativeText(const FString &Message) {
  if (InitiativeText) {
    InitiativeText->SetText(FText::FromString(Message));
    InitiativeText->SetVisibility(ESlateVisibility::Visible);
    if (UWorld *World = GetWorld()) {
      FTimerManager &TimerManager = World->GetTimerManager();
      TimerManager.ClearTimer(InitiativeTimerHandle);

      const TWeakObjectPtr<USkaldMainHUDWidget> WeakThis(this);
      FTimerDelegate TimerDelegate;
      TimerDelegate.BindLambda([WeakThis]() {
        if (WeakThis.IsValid()) {
          WeakThis->HideInitiativeText();
        }
      });

      TimerManager.SetTimer(InitiativeTimerHandle, TimerDelegate, 3.f, false);
    }
  }
}

void USkaldMainHUDWidget::ShowTurnEnded(const FString &PlayerName) {
  const FString Text =
      FString::Printf(TEXT("%s ended their turn"), *PlayerName);
  UpdateInitiativeText(Text);
}

void USkaldMainHUDWidget::QueueDiceResolution(AFighterPawn *Attacker,
                                              AFighterPawn *Defender,
                                              const FDiceRollResult &Result,
                                              bool /*bManualReveal*/) {
  FQueuedDiceResolution Entry;
  Entry.Attacker = MakeWeakObjectPtr(Attacker);
  Entry.Defender = MakeWeakObjectPtr(Defender);
  Entry.Result = Result;
  PendingDiceResolutions.Add(MoveTemp(Entry));

  if (!bDiceResolutionActive) {
    ProcessNextDiceResolution();
  }
}

void USkaldMainHUDWidget::ProcessNextDiceResolution() {
  if (bDiceResolutionActive) {
    return;
  }

  if (PendingDiceResolutions.Num() == 0) {
    return;
  }

  bDiceResolutionActive = true;
  ActiveDiceResolution = PendingDiceResolutions[0];
  PendingDiceResolutions.RemoveAt(0);

  if (!DiceResolutionPanel) {
    OnResolutionComplete.Broadcast(ActiveDiceResolution.Attacker.Get(),
                                   ActiveDiceResolution.Defender.Get(),
                                   ActiveDiceResolution.Result);
    bDiceResolutionActive = false;
    ProcessNextDiceResolution();
    return;
  }

  const FDiceResolutionPanelLayout Layout = ResolveDiceResolutionPanelLayout(
      ActiveDiceResolution.Attacker.Get(), ActiveDiceResolution.Defender.Get(),
      ActiveDiceResolution.Result);
  ApplyDiceResolutionPanelLayoutInternal(Layout);

  DiceResolutionPanel->BeginResolution(ActiveDiceResolution.Result);
}

void USkaldMainHUDWidget::HandleDicePanelResolved(
    const FDiceRollResult &Result) {
  if (!bDiceResolutionActive) {
    OnResolutionComplete.Broadcast(nullptr, nullptr, Result);
    return;
  }

  OnResolutionComplete.Broadcast(ActiveDiceResolution.Attacker.Get(),
                                 ActiveDiceResolution.Defender.Get(),
                                 ActiveDiceResolution.Result);

  bDiceResolutionActive = false;
  ActiveDiceResolution = FQueuedDiceResolution();

  ProcessNextDiceResolution();
}

void USkaldMainHUDWidget::HandleDiceOutcomeRevealed(
    const FDiceRollOutcome &Outcome, int32 RevealIndex) {
  if (!bDiceResolutionActive) {
    OnDiceOutcomeRevealed.Broadcast(nullptr, nullptr, Outcome, RevealIndex);
    return;
  }

  OnDiceOutcomeRevealed.Broadcast(ActiveDiceResolution.Attacker.Get(),
                                  ActiveDiceResolution.Defender.Get(), Outcome,
                                  RevealIndex);
}

void USkaldMainHUDWidget::HideInitiativeText() {
  if (InitiativeText) {
    InitiativeText->SetVisibility(ESlateVisibility::Collapsed);
  }
}

void USkaldMainHUDWidget::UpdateDeployableUnits(int32 UnitsRemaining) {
  if (DeployableUnitsText) {
    const FString Text =
        FString::Printf(TEXT("Deployable: %d"), UnitsRemaining);
    DeployableUnitsText->SetText(FText::FromString(Text));
    DeployableUnitsText->SetVisibility(ESlateVisibility::Visible);
  }
}

void USkaldMainHUDWidget::UpdateResources(int32 ResourceAmount) {
  if (ResourcesText) {
    const FString Text = FString::Printf(TEXT("Resources: %d"), ResourceAmount);
    ResourcesText->SetText(FText::FromString(Text));
    ResourcesText->SetVisibility(ESlateVisibility::Visible);
  }
}

void USkaldMainHUDWidget::ShowErrorMessage(const FString &Message) {
  if (GEngine) {
    GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, Message);
  }
  BP_ShowErrorMessage(Message);
}

void USkaldMainHUDWidget::ShowStatusMessage(const FText &Message,
                                            float DisplayDuration) {
  if (Message.IsEmpty()) {
    HideStatusMessage();
    return;
  }

  CachedStatusMessage = Message;
  CachedStatusMessageDuration = DisplayDuration;
  bStatusMessageVisible = true;

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(StatusMessageTimerHandle);

    if (DisplayDuration > KINDA_SMALL_NUMBER) {
      World->GetTimerManager().SetTimer(
          StatusMessageTimerHandle, this,
          &USkaldMainHUDWidget::HideStatusMessage, DisplayDuration, false);
    }
  }

  BP_ShowStatusMessage(Message, DisplayDuration);
}

void USkaldMainHUDWidget::HideStatusMessage() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(StatusMessageTimerHandle);
  }

  CachedStatusMessage = FText::GetEmpty();
  CachedStatusMessageDuration = 0.f;
  bStatusMessageVisible = false;

  BP_HideStatusMessage();
}

void USkaldMainHUDWidget::ClearTerritoryHighlights() {
  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    if (SelectedSourceID != -1) {
      if (ATerritory *Source = WorldMap->GetTerritoryById(SelectedSourceID)) {
        Source->Deselect();
      }
    }
    if (SelectedTargetID != -1) {
      if (ATerritory *Target = WorldMap->GetTerritoryById(SelectedTargetID)) {
        Target->Deselect();
      }
    }
  }

  for (ATerritory *Terr : HighlightedTerritories) {
    if (Terr) {
      Terr->Deselect();
    }
  }
  HighlightedTerritories.Empty();
}

FText USkaldMainHUDWidget::ResolveSelectionPromptText(const FName &Key,
                                                      const FText &Default) const {
  if (const FText *Override = SelectionPromptOverrides.Find(Key)) {
    if (!Override->IsEmpty()) {
      return *Override;
    }
  }

  return Default;
}

void USkaldMainHUDWidget::ShowSelectionPromptMessage(const FText &Message,
                                                     bool bShow) {
  PendingSelectionPromptText = bShow ? Message : FText::GetEmpty();
  bPendingSelectionPromptVisible = bShow;

  UTextBlock *PromptLabel = GetSelectionPromptTextBlock();
  if (!PromptLabel) {
    return;
  }

  if (!bShow || bAwaitingStrategicInitiative || bSelectionPromptSuppressed) {
    PromptLabel->SetText(FText::GetEmpty());
    PromptLabel->SetVisibility(ESlateVisibility::Collapsed);
    return;
  }

  PromptLabel->SetText(Message);
  PromptLabel->SetVisibility(ESlateVisibility::Visible);
}

void USkaldMainHUDWidget::ShowSelectionErrorMessage(const FText &Message) {
  ShowSelectionPromptMessage(Message);
  ShowErrorMessage(Message.ToString());
}

void USkaldMainHUDWidget::ShowArmyPlacementLimitWarning(const FText &Message) {
  const FString MessageString = Message.ToString();
  ShowSelectionPromptMessage(Message);

  if (GEngine) {
    GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, MessageString);
  }
  BP_ShowErrorMessage(MessageString);

  if (UWorld *World = GetWorld()) {
    if (!bArmyPlacementWarningActive) {
      CachedSelectionPromptText = PendingSelectionPromptText;
      bCachedSelectionPromptVisible = bPendingSelectionPromptVisible;
    }

    bArmyPlacementWarningActive = true;

    World->GetTimerManager().ClearTimer(ArmyPlacementWarningTimerHandle);
    World->GetTimerManager().SetTimer(
        ArmyPlacementWarningTimerHandle, this,
        &USkaldMainHUDWidget::HandleArmyPlacementWarningExpired, 2.0f, false);
  }
}

void USkaldMainHUDWidget::HandleArmyPlacementWarningExpired() {
  bArmyPlacementWarningActive = false;

  if (bCachedSelectionPromptVisible) {
    ShowSelectionPromptMessage(CachedSelectionPromptText, true);
  } else {
    ShowSelectionPromptMessage(FText::GetEmpty(), false);
  }

  CachedSelectionPromptText = FText::GetEmpty();
  bCachedSelectionPromptVisible = false;
}

void USkaldMainHUDWidget::ApplyPendingSelectionPrompt() {
  UTextBlock *PromptLabel = GetSelectionPromptTextBlock();
  if (!PromptLabel) {
    return;
  }

  if (!bPendingSelectionPromptVisible || bAwaitingStrategicInitiative ||
      bSelectionPromptSuppressed) {
    PromptLabel->SetText(FText::GetEmpty());
    PromptLabel->SetVisibility(ESlateVisibility::Collapsed);
    return;
  }

  PromptLabel->SetText(PendingSelectionPromptText);
  PromptLabel->SetVisibility(ESlateVisibility::Visible);
}

bool USkaldMainHUDWidget::IsLocalPlayersTurn() const {
  return CurrentPlayerID != -1 && LocalPlayerID != -1 &&
         CurrentPlayerID == LocalPlayerID;
}

void USkaldMainHUDWidget::SetSelectionPromptSuppressed(
    bool bShouldSuppress) {
  if (bSelectionPromptSuppressed == bShouldSuppress) {
    return;
  }

  bSelectionPromptSuppressed = bShouldSuppress;

  if (bSelectionPromptSuppressed) {
    if (UTextBlock *PromptLabel = GetSelectionPromptTextBlock()) {
      PromptLabel->SetText(FText::GetEmpty());
      PromptLabel->SetVisibility(ESlateVisibility::Collapsed);
    }
  } else {
    ApplyPendingSelectionPrompt();
  }
}

UTextBlock *USkaldMainHUDWidget::GetSelectionPromptTextBlock() const {
  if (SelectionPromptText) {
    return SelectionPromptText;
  }
  return SelectionPrompt;
}

ATerritory *USkaldMainHUDWidget::GetCurrentlySelectedTerritory() const {
  if (const UWorld *World = GetWorld()) {
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator();
         It; ++It) {
      if (const ASkaldPlayerController *PC =
              Cast<ASkaldPlayerController>(*It)) {
        if (!PC->IsLocalController()) {
          continue;
        }

        if (const ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>()) {
          return PS->SelectedTerritory.Get();
        }
      }
    }
  }

  return nullptr;
}

void USkaldMainHUDWidget::BeginAttackSelection() {
  ClearTerritoryHighlights();
  bSelectingForAttack = true;
  bSelectingForMove = false;
  SelectedSourceID = -1;
  SelectedTargetID = -1;

  if (ActiveConfirmWidget) {
    ActiveConfirmWidget->RemoveFromParent();
    ActiveConfirmWidget = nullptr;
  }

  const FText Prompt = ResolveSelectionPromptText(
      SkaldSelectionPromptKeys::AttackSelectOwnedTerritory,
      NSLOCTEXT("SkaldHUD", "AttackSelectOwnedTerritory",
                "Select an owned territory to attack from."));
  ShowSelectionPromptMessage(Prompt);

  if (ATerritory *Preselected = GetCurrentlySelectedTerritory()) {
    OnTerritoryClickedUI(Preselected);
  }
}

void USkaldMainHUDWidget::SubmitAttack(int32 FromID, int32 ToID, int32 ArmySent,
                                       bool bUseSiege) {
  OnAttackRequested.Broadcast(FromID, ToID, ArmySent, bUseSiege);
  CancelAttackSelection();
}

void USkaldMainHUDWidget::CancelAttackSelection() {
  ClearTerritoryHighlights();
  if (ActiveConfirmWidget) {
    ActiveConfirmWidget->RemoveFromParent();
    ActiveConfirmWidget = nullptr;
  }
  HidePrepareForBattleDialog();
  if (CurrentPhase == ETurnPhase::Reinforcement) {
    const FText Prompt = ResolveSelectionPromptText(
        SkaldSelectionPromptKeys::ReinforcementOwnedCapitalPrompt,
        ReinforcementSelectionPromptText);
    ShowSelectionPromptMessage(Prompt);
  } else if (CurrentPhase == ETurnPhase::ArmyPlacement) {
    const FText Prompt = ResolveSelectionPromptText(
        SkaldSelectionPromptKeys::CancelAttackArmyPlacementPrompt,
        NSLOCTEXT("SkaldHUD", "CancelAttackArmyPlacementPrompt",
                  "Select an owned territory."));
    ShowSelectionPromptMessage(Prompt);
  } else {
    const FText Prompt = ResolveSelectionPromptText(
        SkaldSelectionPromptKeys::CancelAttackPrompt,
        NSLOCTEXT("SkaldHUD", "CancelAttackPrompt",
                  "Press Attack, then select an owned territory."));
    ShowSelectionPromptMessage(Prompt);
  }
  bSelectingForAttack = false;
  SelectedSourceID = -1;
  SelectedTargetID = -1;
}

void USkaldMainHUDWidget::BeginMoveSelection() {
  ClearTerritoryHighlights();
  bSelectingForMove = true;
  bSelectingForAttack = false;
  SelectedSourceID = -1;
  SelectedTargetID = -1;
  const FText Prompt = ResolveSelectionPromptText(
      SkaldSelectionPromptKeys::MoveSelectSourceTerritory,
      NSLOCTEXT("SkaldHUD", "MoveSelectSourceTerritory",
                "Select a territory to move troops from."));
  ShowSelectionPromptMessage(Prompt);

  if (ATerritory *Preselected = GetCurrentlySelectedTerritory()) {
    OnTerritoryClickedUI(Preselected);
  }
}

void USkaldMainHUDWidget::SubmitMove(int32 FromID, int32 ToID, int32 Troops) {
  OnMoveRequested.Broadcast(FromID, ToID, Troops);
  CancelMoveSelection();
}

void USkaldMainHUDWidget::HandleMoveOutcome(bool bSuccess,
                                            const FString &Message) {
  if (bSuccess) {
    ShowSelectionPromptMessage(FText::FromString(Message));
    return;
  }

  ShowErrorMessage(Message);
  ResetMoveSelectionAfterInvalidAttempt();
}

void USkaldMainHUDWidget::CancelMoveSelection() {
  ClearTerritoryHighlights();
  bSelectingForMove = false;
  SelectedSourceID = -1;
  SelectedTargetID = -1;
  if (CurrentPhase == ETurnPhase::Movement) {
    const FText Prompt = ResolveSelectionPromptText(
        SkaldSelectionPromptKeys::CancelMovePrompt,
        NSLOCTEXT("SkaldHUD", "CancelMovePrompt",
                  "Press Move, then select an owned territory."));
    ShowSelectionPromptMessage(Prompt);
  } else {
    ShowSelectionPromptMessage(FText::GetEmpty(), false);
  }
}

void USkaldMainHUDWidget::ResetMoveSelectionAfterInvalidAttempt() {
  ClearTerritoryHighlights();
  SelectedSourceID = -1;
  SelectedTargetID = -1;
  if (CurrentPhase == ETurnPhase::Movement) {
    bSelectingForMove = true;
    bSelectingForAttack = false;
    const FText Prompt = ResolveSelectionPromptText(
        SkaldSelectionPromptKeys::ResetMoveSelectSource,
        NSLOCTEXT("SkaldHUD", "ResetMoveSelectSource",
                  "Select a territory to move troops from."));
    ShowSelectionPromptMessage(Prompt);
  } else {
    bSelectingForMove = false;
  }
}

void USkaldMainHUDWidget::OnTerritoryClickedUI(ATerritory *Territory) {
  if (!Territory) {
    return;
  }

  ASkaldPlayerState *LocalPS = nullptr;
  if (APlayerController *PC = GetOwningPlayer()) {
    LocalPS = PC->GetPlayerState<ASkaldPlayerState>();
  }

  const int32 LocalStableId =
      LocalPS ? LocalPS->GetStablePlayerId() : static_cast<int32>(INDEX_NONE);
  const int32 OwnerStableId =
      IsValid(Territory->OwningPlayer)
          ? Territory->OwningPlayer->GetStablePlayerId()
          : static_cast<int32>(INDEX_NONE);

  const bool bOwnedByLocal =
      LocalStableId != static_cast<int32>(INDEX_NONE) &&
      LocalStableId == OwnerStableId;

  if (bSelectingRetreatDestination) {
    if (bAwaitingRetreatConfirmation) {
      return;
    }

    if (!RetreatCandidateIds.Contains(Territory->TerritoryID)) {
      const FText Error = NSLOCTEXT(
          "SkaldHUD", "RetreatInvalidTarget",
          "Select a highlighted territory to retreat to.");
      ShowSelectionErrorMessage(Error);
      return;
    }

    bAwaitingRetreatConfirmation = true;
    Territory->Select(LocalPlayerID);
    OnRetreatDestinationChosen.Broadcast(Territory->TerritoryID);
    return;
  }

  if (bSelectingForAttack) {
    // If player is mid-selection and clicks a different friendly source,
    // clear previous highlights so we don't accumulate stale visuals.
    if (SelectedSourceID != -1 && SelectedTargetID == -1 && bOwnedByLocal &&
        Territory->TerritoryID != SelectedSourceID) {
      for (ATerritory *T : HighlightedTerritories) {
        if (T) {
          T->Deselect();
        }
      }
      HighlightedTerritories.Empty();
      if (AWorldMap *WorldMap =
              Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
                  GetWorld(), AWorldMap::StaticClass()))) {
        if (ATerritory *Source = WorldMap->GetTerritoryById(SelectedSourceID)) {
          Source->Deselect();
        }
      }
      SelectedSourceID = -1;
    }

    if (SelectedSourceID == -1) {
      if (bOwnedByLocal && Territory->ArmyUnits > 1) {
        SelectedSourceID = Territory->TerritoryID;
        Territory->Select();
        const FText Prompt = ResolveSelectionPromptText(
            SkaldSelectionPromptKeys::ChooseEnemyTerritoryPrompt,
            NSLOCTEXT("SkaldHUD", "ChooseEnemyTerritoryPrompt",
                      "Choose enemy territory."));
        ShowSelectionPromptMessage(Prompt);
        HighlightedTerritories.Empty();
        for (ATerritory *Adj : Territory->AdjacentTerritories) {
          if (Adj && Adj->OwningPlayer != Territory->OwningPlayer) {
            Adj->Select();
            HighlightedTerritories.Add(Adj);
          }
        }
      } else if (bOwnedByLocal) {
        ShowErrorMessage(TEXT("Need more than one unit to attack"));
      }
      return;
    }

    // Source selected: only allow choosing highlighted enemy territories
    const bool bIsHighlighted =
        HighlightedTerritories.ContainsByPredicate([Territory](ATerritory *T) {
          return T && T->TerritoryID == Territory->TerritoryID;
        });
    if (bIsHighlighted) {
      SelectedTargetID = Territory->TerritoryID;
      ShowSelectionPromptMessage(FText::GetEmpty(), false);
      if (ConfirmAttackWidgetClass) {
        APlayerController* OwningPlayerController = GetOwningPlayer();
        if (!OwningPlayerController) {
          UE_LOG(LogSkald, Warning,
                 TEXT("OnTerritoryClickedUI: missing owning player controller for confirm widget"));
          ShowErrorMessage(TEXT("Could not open confirm attack UI"));
          return;
        }
        LogHUDWidgetCreationContext(this, TEXT("OnTerritoryClickedUI.ConfirmAttack"));
        ActiveConfirmWidget = CreateWidget<UConfirmAttackWidget>(
            OwningPlayerController, ConfirmAttackWidgetClass);
        if (ActiveConfirmWidget) {
          int32 MaxUnits = 1;
          if (AWorldMap *WorldMap =
                  Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
                      GetWorld(), AWorldMap::StaticClass()))) {
            if (ATerritory *Source =
                    WorldMap->GetTerritoryById(SelectedSourceID)) {
              MaxUnits = Source->ArmyUnits - 1;
            }
          }
          ActiveConfirmWidget->Setup(MaxUnits);
          ActiveConfirmWidget->AddToViewport();
          if (ActiveConfirmWidget->ApproveButton) {
            ActiveConfirmWidget->ApproveButton->OnClicked.AddDynamic(
                this, &USkaldMainHUDWidget::HandleAttackApproved);
          }
          if (ActiveConfirmWidget->CancelButton) {
            ActiveConfirmWidget->CancelButton->OnClicked.AddDynamic(
                this, &USkaldMainHUDWidget::CancelAttackSelection);
          }
        } else {
          UE_LOG(LogSkald, Warning,
                 TEXT("OnTerritoryClickedUI: failed to create confirm widget"));
          ShowErrorMessage(TEXT("Could not open confirm attack UI"));
        }
      } else {
        UE_LOG(LogSkald, Warning,
               TEXT("OnTerritoryClickedUI: ConfirmAttackWidgetClass null"));
        ShowErrorMessage(TEXT("Confirm attack widget missing"));
      }
    } else {
      UE_LOG(
          LogSkald, Warning,
          TEXT("OnTerritoryClickedUI: territory %d not highlighted for attack"),
          Territory->TerritoryID);
      ShowErrorMessage(TEXT("Target not attackable"));
    }
    return;
  } else if (bSelectingForMove) {
    if (!bOwnedByLocal) {
      const FText Error = ResolveSelectionPromptText(
          SkaldSelectionPromptKeys::MoveOwnTerritoriesOnly,
          NSLOCTEXT("SkaldHUD", "MoveOwnTerritoriesOnly",
                    "You may only move between your own territories."));
      ShowSelectionErrorMessage(Error);
      return;
    }

    if (SelectedSourceID == -1 || Territory->TerritoryID == SelectedSourceID) {
      if (Territory->ArmyUnits <= 1) {
        const FText Error = ResolveSelectionPromptText(
            SkaldSelectionPromptKeys::MoveNeedMoreTroops,
            NSLOCTEXT("SkaldHUD", "MoveNeedMoreTroops",
                      "Need more than one unit to move troops."));
        ShowSelectionErrorMessage(Error);
        return;
      }

      ClearTerritoryHighlights();
      SelectedSourceID = Territory->TerritoryID;
      SelectedTargetID = -1;
      Territory->Select();

      HighlightedTerritories.Empty();
      TSet<ATerritory *> Visited;
      TQueue<ATerritory *> Frontier;
      Visited.Add(Territory);
      Frontier.Enqueue(Territory);

      while (!Frontier.IsEmpty()) {
        ATerritory *Current = nullptr;
        Frontier.Dequeue(Current);
        if (!Current) {
          continue;
        }

        for (ATerritory *Neighbor : Current->AdjacentTerritories) {
          if (!Neighbor || Visited.Contains(Neighbor) ||
              Neighbor->OwningPlayer != Territory->OwningPlayer) {
            continue;
          }

          Visited.Add(Neighbor);
          Frontier.Enqueue(Neighbor);
          Neighbor->Select();
          HighlightedTerritories.Add(Neighbor);
        }
      }

      if (HighlightedTerritories.Num() == 0) {
        const FText Error = ResolveSelectionPromptText(
            SkaldSelectionPromptKeys::MoveNoConnectedTerritory,
            NSLOCTEXT("SkaldHUD", "MoveNoConnectedTerritory",
                      "No connected friendly territory to move into."));
        ShowSelectionErrorMessage(Error);
        SelectedSourceID = -1;
        Territory->Deselect();
        return;
      }

      const FText Prompt = ResolveSelectionPromptText(
          SkaldSelectionPromptKeys::SelectConnectedTerritoryPrompt,
          NSLOCTEXT("SkaldHUD", "SelectConnectedTerritoryPrompt",
                    "Select a connected territory to receive troops."));
      ShowSelectionPromptMessage(Prompt);
      return;
    }

    const bool bIsHighlighted = HighlightedTerritories.ContainsByPredicate(
        [Territory](ATerritory *T) {
          return T && T->TerritoryID == Territory->TerritoryID;
        });

    if (!bIsHighlighted) {
      const FText Error = ResolveSelectionPromptText(
          SkaldSelectionPromptKeys::MoveInvalidTarget,
          NSLOCTEXT("SkaldHUD", "MoveInvalidTarget",
                    "Target not valid for movement."));
      ShowSelectionErrorMessage(Error);
      return;
    }

    SelectedTargetID = Territory->TerritoryID;

    AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
        GetWorld(), AWorldMap::StaticClass()));
    if (!WorldMap) {
      const FText Error = ResolveSelectionPromptText(
          SkaldSelectionPromptKeys::MoveWorldMapMissing,
          NSLOCTEXT("SkaldHUD", "MoveWorldMapMissing",
                    "World map not found."));
      ShowSelectionErrorMessage(Error);
      CancelMoveSelection();
      return;
    }

    ATerritory *Source = WorldMap->GetTerritoryById(SelectedSourceID);
    ATerritory *Target = WorldMap->GetTerritoryById(SelectedTargetID);
    if (!Source || !Target) {
      const FText Error = ResolveSelectionPromptText(
          SkaldSelectionPromptKeys::MoveInvalidSelection,
          NSLOCTEXT("SkaldHUD", "MoveInvalidSelection",
                    "Invalid territory selection."));
      ShowSelectionErrorMessage(Error);
      CancelMoveSelection();
      return;
    }

    const int32 MaxMovable = Source->ArmyUnits - 1;
    if (MaxMovable <= 0) {
      const FText Error = ResolveSelectionPromptText(
          SkaldSelectionPromptKeys::MoveNoTroopsAvailable,
          NSLOCTEXT("SkaldHUD", "MoveNoTroopsAvailable",
                    "No troops available to move."));
      ShowSelectionErrorMessage(Error);
      CancelMoveSelection();
      return;
    }

    if (!DeployWidgetClass) {
      const FText Error = ResolveSelectionPromptText(
          SkaldSelectionPromptKeys::MoveDeployWidgetUnavailable,
          NSLOCTEXT("SkaldHUD", "MoveDeployWidgetUnavailable",
                    "Deploy widget unavailable."));
      ShowSelectionErrorMessage(Error);
      CancelMoveSelection();
      return;
    }

    if (ActiveDeployWidget && ActiveDeployWidget->IsInViewport()) {
      ActiveDeployWidget->RemoveFromParent();
      ActiveDeployWidget = nullptr;
    }

    if (!GetOwningLocalPlayer()) {
      UE_LOG(LogSkald, Warning,
             TEXT("OnTerritoryClickedUI: missing owning local player for deploy widget"));
      const FText Error = ResolveSelectionPromptText(
          SkaldSelectionPromptKeys::MoveDeployUICreationFailed,
          NSLOCTEXT("SkaldHUD", "MoveDeployUICreationFailed",
                    "Could not open deploy UI."));
      ShowSelectionErrorMessage(Error);
      CancelMoveSelection();
      return;
    }

    LogHUDWidgetCreationContext(this, TEXT("OnTerritoryClickedUI.DeployMove"));
    ActiveDeployWidget =
        CreateWidget<UDeployWidget>(GetOwningPlayer(), DeployWidgetClass);
    if (!ActiveDeployWidget) {
      const FText Error = ResolveSelectionPromptText(
          SkaldSelectionPromptKeys::MoveDeployUICreationFailed,
          NSLOCTEXT("SkaldHUD", "MoveDeployUICreationFailed",
                    "Could not open deploy UI."));
      ShowSelectionErrorMessage(Error);
      CancelMoveSelection();
      return;
    }

    ActiveDeployWidget->SetupTransfer(Source, Target, this, MaxMovable);
    ActiveDeployWidget->AddToViewport();
    if (APlayerController *FocusPC = GetOwningPlayer()) {
      FocusWidgetUIOnly(FocusPC, ActiveDeployWidget);
    }

    const FText Prompt = ResolveSelectionPromptText(
        SkaldSelectionPromptKeys::ChooseTroopsToMovePrompt,
        NSLOCTEXT("SkaldHUD", "ChooseTroopsToMovePrompt",
                  "Choose how many troops to move."));
    ShowSelectionPromptMessage(Prompt);
  } else if (CurrentPhase == ETurnPhase::Reinforcement ||
             CurrentPhase == ETurnPhase::ArmyPlacement) {
    SelectedSourceID = Territory->TerritoryID;
    if (DeployButton) {
      int32 EffectiveLocalPlayerId = LocalPlayerID;
      if (EffectiveLocalPlayerId == -1) {
        EffectiveLocalPlayerId = ResolveLocalPlayerId();
        if (EffectiveLocalPlayerId != -1) {
          LocalPlayerID = EffectiveLocalPlayerId;
        }
      }
      const bool bIsMyTurn =
          (CurrentPlayerID != -1 && EffectiveLocalPlayerId != -1 &&
           CurrentPlayerID == EffectiveLocalPlayerId);
      const bool bIsCapital = Territory->bIsCapital;
      const bool bIsArmyPlacement = CurrentPhase == ETurnPhase::ArmyPlacement;
      const bool bCanDeployHere =
          bOwnedByLocal && (bIsArmyPlacement || bIsCapital);
      const bool bShouldShowDeploy = bIsMyTurn && bCanDeployHere;
      DeployButton->SetVisibility(
          bShouldShowDeploy ? ESlateVisibility::Visible
                            : ESlateVisibility::Collapsed);
      DeployButton->SetIsEnabled(bShouldShowDeploy);
    }
    if (CurrentPhase == ETurnPhase::Reinforcement) {
      if (bOwnedByLocal && !Territory->bIsCapital) {
        const FText Error = ResolveSelectionPromptText(
            SkaldSelectionPromptKeys::ReinforcementCapitalRestriction,
            NSLOCTEXT("SkaldHUD", "ReinforcementCapitalRestriction",
                      "Reinforcements can only be placed on owned capitals."));
        ShowSelectionErrorMessage(Error);
      } else if (bOwnedByLocal) {
        const FText Prompt = ResolveSelectionPromptText(
            SkaldSelectionPromptKeys::ReinforcementOwnedCapitalPrompt,
            ReinforcementSelectionPromptText);
        ShowSelectionPromptMessage(Prompt);
      }
    } else if (CurrentPhase == ETurnPhase::ArmyPlacement && bOwnedByLocal) {
      const FText Prompt = ResolveSelectionPromptText(
          SkaldSelectionPromptKeys::ArmyPlacementSelectOwnedTerritory,
          NSLOCTEXT("SkaldHUD", "ArmyPlacementSelectOwnedTerritory",
                    "Select an owned territory."));
      ShowSelectionPromptMessage(Prompt);
    }
  } else if (CurrentPhase == ETurnPhase::Engineering && bOwnedByLocal &&
             Territory->bIsCapital) {
    OnEngineeringRequested.Broadcast(Territory->TerritoryID, 0);
  } else if (CurrentPhase == ETurnPhase::Treasure && bOwnedByLocal &&
             Territory->bHasTreasure) {
    OnDigTreasureRequested.Broadcast(Territory->TerritoryID);
  }
}

void USkaldMainHUDWidget::BuildSiege(int32 TerritoryID,
                                     ESiegeWeapon SiegeType) {
  OnBuildSiegeRequested.Broadcast(TerritoryID, SiegeType);
}

void USkaldMainHUDWidget::SetUseSiegeForNextAttack(bool bEnable) {
  bUseSiegeForNextAttack = bEnable;
}

int32 USkaldMainHUDWidget::ResolveLocalPlayerId() const {
  if (const APlayerController *PC = GetOwningPlayer()) {
    if (const ASkaldPlayerState *LocalPS =
            PC->GetPlayerState<ASkaldPlayerState>()) {
      return LocalPS->GetStablePlayerId();
    }
  }
  return -1;
}

void USkaldMainHUDWidget::HandlePlayersUpdated() {
  if (!GameState) {
    return;
  }

  int32 NewLocalPlayerId = LocalPlayerID;
  const int32 ResolvedLocalId = ResolveLocalPlayerId();
  if (ResolvedLocalId != -1) {
    NewLocalPlayerId = ResolvedLocalId;
  }

  TArray<FS_PlayerData> Players;
  for (APlayerState *PSBase : GameState->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      FS_PlayerData Data;
      Data.PlayerID = PS->GetStablePlayerId();
      Data.PlayerName =
          PS->GetResolvedPlayerName(TEXT("SkaldMainHUDWidget::RefreshPlayers"));
      Data.IsAI = PS->bIsAI;
      Data.Faction = PS->Faction;
      Players.Add(Data);
    }
  }

  RefreshPlayerList(Players);

  const bool bLocalIdChanged = (NewLocalPlayerId != LocalPlayerID);
  LocalPlayerID = NewLocalPlayerId;
  if (bLocalIdChanged) {
    SyncPhaseButtons(CurrentPlayerID == LocalPlayerID);
  }
}

void USkaldMainHUDWidget::HandleTurnIndexChanged(int32 /*NewTurnIndex*/) {
  // Derive current player ID and approximate turn number from GameState.
  int32 NewPlayerID = -1;
  int32 NewTurnNumber = TurnNumber;
  if (GameState) {
    if (ASkaldPlayerState *PS = GameState->GetCurrentPlayer()) {
      NewPlayerID = PS->GetStablePlayerId();
    }

    const int32 PlayerCount = GameState->PlayerArray.Num();
    if (PlayerCount > 0) {
      NewTurnNumber = GameState->CurrentTurnIndex / PlayerCount + 1;
    }
  }

  // Update cached turn state before refreshing widgets.
  CurrentPlayerID = NewPlayerID;
  TurnNumber = NewTurnNumber;
  ClearStrategicInitiativeWaitIfNeeded();

  // Update widget text and cached state.
  BP_SetTurnText(TurnNumber, CurrentPlayerID);

  // Update buttons for the new player and show a brief turn message.
  if (LocalPlayerID == -1) {
    const int32 ResolvedLocalId = ResolveLocalPlayerId();
    if (ResolvedLocalId != -1) {
      LocalPlayerID = ResolvedLocalId;
    }
  }
  const bool bIsMyTurn = (CurrentPlayerID == LocalPlayerID);
  SyncPhaseButtons(bIsMyTurn);
  ShowTurnMessage(bIsMyTurn);
  // (UpdateTurnBanner already calls SyncPhaseButtons, so this is just UX
  // sugar.)
}

void USkaldMainHUDWidget::SyncPhaseButtons(bool bIsMyTurn) {
  BP_SetPhaseButtons(CurrentPhase, bIsMyTurn);

  auto SetButtonState = [](UButton *Button, bool bShouldShow,
                           bool bShouldEnable) {
    if (!Button) {
      return;
    }

    Button->SetVisibility(bShouldShow ? ESlateVisibility::Visible
                                      : ESlateVisibility::Collapsed);
    Button->SetIsEnabled(bShouldEnable && bShouldShow);
  };

  if (bAwaitingStrategicInitiative) {
    // The strategic initiative overlay is only supposed to block interaction
    // while it is visible. In some race conditions (for example when an AI
    // opponent wins initiative and completes their automatic deployment before
    // the local client's UI updates) the overlay may already be hidden while
    // the cached flag is still set. This prevented the deployment/end turn
    // buttons from ever becoming visible on the player's turn. If the overlay
    // is no longer active we drop the stale flag so normal button syncing can
    // proceed.
    if (!IsStrategicInitiativeOverlayActive()) {
      bAwaitingStrategicInitiative = false;
    }
  }

  if (bAwaitingStrategicInitiative) {
    SetButtonState(AttackButton, false, false);
    SetButtonState(MoveButton, false, false);
    SetButtonState(DeployButton, false, false);
    SetButtonState(EndPhaseButton, false, false);
    SetButtonState(EndTurnButton, false, false);
    return;
  }

  const bool bShowAttackButton = bIsMyTurn && CurrentPhase == ETurnPhase::Attack;
  SetButtonState(AttackButton, bShowAttackButton, bShowAttackButton);

  const bool bShowMoveButton =
      bIsMyTurn && CurrentPhase == ETurnPhase::Movement;
  SetButtonState(MoveButton, bShowMoveButton, bShowMoveButton);

  const bool bShowDeployButton =
      bIsMyTurn &&
      (CurrentPhase == ETurnPhase::Reinforcement ||
       CurrentPhase == ETurnPhase::ArmyPlacement);
  SetButtonState(DeployButton, bShowDeployButton, bShowDeployButton);

  bool bHasPendingBattle = false;
  if (bIsMyTurn) {
    if (ASkaldPlayerController *PlayerController =
            Cast<ASkaldPlayerController>(GetOwningPlayer())) {
      if (ATurnManager *Manager = PlayerController->GetTurnManager()) {
        bHasPendingBattle = Manager->HasPendingBattlePreparation();
      }
    }
  }

  const bool bShowEndPhaseButton =
      bIsMyTurn && !bHasPendingBattle && CurrentPhase != ETurnPhase::EndTurn;
  SetButtonState(EndPhaseButton, bShowEndPhaseButton, bShowEndPhaseButton);

  const bool bShowEndTurnButton =
      bIsMyTurn && CurrentPhase == ETurnPhase::EndTurn;
  SetButtonState(EndTurnButton, bShowEndTurnButton, bShowEndTurnButton);
}

void USkaldMainHUDWidget::HandleAttackApproved() {
  if (!ActiveConfirmWidget) {
    return;
  }

  const int32 SourceID = SelectedSourceID;
  const int32 TargetID = SelectedTargetID;
  int32 ArmyCount = ActiveConfirmWidget->ArmyCount;
  bool bTargetIsCapital = false;
  int32 MaxAvailableUnits = 0;

  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    if (ATerritory *Source = WorldMap->GetTerritoryById(SourceID)) {
      MaxAvailableUnits = Source->ArmyUnits - 1;
      if (MaxAvailableUnits < 0) {
        MaxAvailableUnits = 0;
      }

      if (MaxAvailableUnits <= 0) {
        ShowErrorMessage(TEXT("Need more than one unit to attack"));
        CancelAttackSelection();
        return;
      }

      ArmyCount = FMath::Clamp(ArmyCount, 1, MaxAvailableUnits);
      if (ActiveConfirmWidget) {
        ActiveConfirmWidget->Setup(MaxAvailableUnits);
      }
    }
    if (ATerritory *Target = WorldMap->GetTerritoryById(TargetID)) {
      bTargetIsCapital = Target->bIsCapital;
    }
  }

  if (!SkaldHelpers::MeetsCapitalAttackRequirement(bTargetIsCapital,
                                                   ArmyCount)) {
    const FString Error = FString::Printf(
        TEXT("Must send at least %d units to attack a capital."),
        SkaldConstants::CapitalAttackArmyRequirement);
    ShowSelectionPromptMessage(FText::FromString(Error));
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, Error);
    }
    return;
  }

  // Remove the confirmation widget now that the attack is approved
  ActiveConfirmWidget->RemoveFromParent();
  ActiveConfirmWidget = nullptr;

  SubmitAttack(SourceID, TargetID, ArmyCount, bUseSiegeForNextAttack);

  bUseSiegeForNextAttack = false;
}

void USkaldMainHUDWidget::HandlePrepareForBattleClicked() {
  if (bPrepareForBattleReadySent) {
    return;
  }

  bPrepareForBattleReadySent = true;
  if (ActivePrepareForBattleWidget &&
      ActivePrepareForBattleWidget->PrepareForBattleButton) {
    ActivePrepareForBattleWidget->PrepareForBattleButton->SetIsEnabled(false);
  }

  OnPrepareForBattleReady.Broadcast();
}

void USkaldMainHUDWidget::HandleDeployClicked() {
  APlayerController *PC = GetOwningPlayer();
  if (!PC) {
    UE_LOG(LogSkald, Warning,
           TEXT("HandleDeployClicked failed: no owning PlayerController"));
    ShowErrorMessage(TEXT("No player controller"));
    return;
  }

  ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    UE_LOG(LogSkald, Warning,
           TEXT("HandleDeployClicked failed: missing PlayerState"));
    ShowErrorMessage(TEXT("Missing player state"));
    return;
  }
  if (PS->DeployableUnits <= 0) {
    UE_LOG(LogSkald, Warning,
           TEXT("HandleDeployClicked failed: no deployable units"));
    ShowErrorMessage(TEXT("No troops to deploy"));
    return;
  }
  if (SelectedSourceID == -1) {
    if (ATerritory *Preselected = GetCurrentlySelectedTerritory()) {
      SelectedSourceID = Preselected->TerritoryID;
    }
  }
  if (SelectedSourceID == -1) {
    UE_LOG(LogSkald, Warning,
           TEXT("HandleDeployClicked failed: no territory selected"));
    ShowErrorMessage(TEXT("No territory selected"));
    return;
  }
  if (!DeployWidgetClass) {
    UE_LOG(LogSkald, Warning,
           TEXT("HandleDeployClicked failed: DeployWidgetClass null"));
    ShowErrorMessage(TEXT("Deploy widget unavailable"));
    return;
  }

  ATerritory *Territory = nullptr;
  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    Territory = WorldMap->GetTerritoryById(SelectedSourceID);
  }

  if (!Territory || !Territory->OwningPlayer ||
      Territory->OwningPlayer->GetStablePlayerId() != PS->GetStablePlayerId()) {
    UE_LOG(LogSkald, Warning,
           TEXT("HandleDeployClicked failed: invalid territory %d"),
           SelectedSourceID);
    ShowErrorMessage(TEXT("Invalid territory selected"));
    return;
  }

  const bool bIsArmyPlacement = CurrentPhase == ETurnPhase::ArmyPlacement;
  if (!bIsArmyPlacement && !Territory->bIsCapital) {
    const FText Error = ResolveSelectionPromptText(
        SkaldSelectionPromptKeys::ReinforcementCapitalRestriction,
        NSLOCTEXT("SkaldHUD", "ReinforcementCapitalRestriction",
                  "Reinforcements can only be placed on owned capitals."));
    ShowSelectionErrorMessage(Error);
    return;
  }

  int32 MaxDeployable = PS->DeployableUnits;
  if (bIsArmyPlacement) {
    const int32 TerritoryId = Territory->TerritoryID;
    const int32 AlreadyPlaced =
        PS->GetArmyPlacementDeploymentForTerritory(TerritoryId);
    const int32 RemainingCapacity =
        FMath::Max(0, Skald::ArmyPlacement::DeployPerTerritoryLimit -
                           AlreadyPlaced);
    if (RemainingCapacity <= 0) {
      const FText WarningText = NSLOCTEXT(
          "SkaldHUD", "ArmyPlacementTerritoryMaxReached",
          "Maximum troop deployment for this territory has been reached.");
      ShowArmyPlacementLimitWarning(WarningText);
      return;
    }
    MaxDeployable = FMath::Min(MaxDeployable, RemainingCapacity);
  }

  if (!GetOwningLocalPlayer()) {
    UE_LOG(LogSkald, Warning,
           TEXT("HandleDeployClicked failed: missing owning local player"));
    ShowErrorMessage(TEXT("Could not open deploy UI"));
    return;
  }

  LogHUDWidgetCreationContext(this, TEXT("HandleDeployClicked.Deploy"));
  ActiveDeployWidget =
      CreateWidget<UDeployWidget>(GetOwningPlayer(), DeployWidgetClass);
  if (ActiveDeployWidget) {
    ActiveDeployWidget->SetupDeployment(Territory, PS, this, MaxDeployable);
    ActiveDeployWidget->AddToViewport();
    if (APlayerController *FocusPC = GetOwningPlayer()) {
      FocusWidgetUIOnly(FocusPC, ActiveDeployWidget);
    }
  } else {
    UE_LOG(LogSkald, Warning,
           TEXT("HandleDeployClicked failed: could not create widget"));
    ShowErrorMessage(TEXT("Could not open deploy UI"));
  }
}

void USkaldMainHUDWidget::ClearDeployWidget() {
  if (!ActiveDeployWidget) {
    return;
  }

  if (ActiveDeployWidget->IsInViewport()) {
    ActiveDeployWidget->RemoveFromParent();
  }

  ActiveDeployWidget = nullptr;

  if (ASkaldPlayerController *PC =
          Cast<ASkaldPlayerController>(GetOwningPlayer())) {
    PC->ShowMainHUD();
  }
}

void USkaldMainHUDWidget::HandleDeploymentCancelled() {
  ClearDeployWidget();

  if (DeployButton) {
    DeployButton->SetVisibility(ESlateVisibility::Collapsed);
    DeployButton->SetIsEnabled(false);
  }

  SelectedSourceID = -1;

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(ArmyPlacementWarningTimerHandle);
  }
  bArmyPlacementWarningActive = false;
  CachedSelectionPromptText = FText::GetEmpty();
  bCachedSelectionPromptVisible = false;

  if (AWorldMap *WorldMap =
          Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
              GetWorld(), AWorldMap::StaticClass()))) {
    if (LocalPlayerID != -1) {
      WorldMap->SelectTerritory(nullptr, false, LocalPlayerID);
    } else {
      WorldMap->SelectTerritory(nullptr, false, INDEX_NONE);
    }
  }

  if (CurrentPhase == ETurnPhase::ArmyPlacement) {
    const FText Prompt = ResolveSelectionPromptText(
        SkaldSelectionPromptKeys::ArmyPlacementOwnedTerritoryPrompt,
        NSLOCTEXT("SkaldHUD", "ArmyPlacementOwnedTerritoryPrompt",
                  "Select an owned territory. You may deploy up to 10 troops per territory."));
    ShowSelectionPromptMessage(Prompt);
  }
}

void USkaldMainHUDWidget::ShowFloatingTextAtLocation(
    const FVector &WorldLocation, const FText &Message,
    const FLinearColor &Tint, float Scale, float LifetimeOverride) {
  UCombatFloaterPoolSubsystem *Pool = ResolveFloaterPool();
  if (!Pool) {
    return;
  }

  if (FloaterWidgetClass) {
    Pool->FloaterWidgetClass = FloaterWidgetClass;
  }

  APlayerController *OwningController = GetOwningPlayer();
  if (!OwningController) {
    return;
  }

  if (UW_FloatingText *Floater = Pool->SpawnFloater(OwningController)) {
    Floater->SetText(Message);
    Floater->SetColorAndOpacity(Tint);
    Floater->SetFloaterOpacity(1.f);
    Floater->SetFloaterScale(Scale);

    FSkaldActiveFloater &Entry = ActiveFloaters.AddDefaulted_GetRef();
    Entry.Floater = Floater;
    Entry.AnchorLocation = WorldLocation;
    Entry.InitialOffset = FVector2D(FMath::RandRange(-20.f, 20.f), 0.f);
    Entry.HorizontalDirection = FMath::RandBool() ? 1.f : -1.f;
    Entry.Lifetime = LifetimeOverride > 0.f ? LifetimeOverride : FloaterLifetime;
    Entry.Lifetime = FMath::Max(Entry.Lifetime, 0.1f);
    Entry.FadeDuration = FloaterFadeDuration;
    Entry.Elapsed = 0.f;
    Entry.Scale = Scale;

    Floater->UpdateProjection(WorldLocation, Entry.InitialOffset,
                              FloaterClampMargin);
  }
}

void USkaldMainHUDWidget::UpdateActiveFloaters(float DeltaSeconds) {
  if (ActiveFloaters.Num() == 0) {
    return;
  }

  for (int32 Index = ActiveFloaters.Num() - 1; Index >= 0; --Index) {
    FSkaldActiveFloater &Entry = ActiveFloaters[Index];
    UW_FloatingText *Floater = Entry.Floater.Get();
    if (!Floater) {
      ActiveFloaters.RemoveAtSwap(Index);
      continue;
    }

    Entry.Elapsed += DeltaSeconds;
    const float Lifetime = FMath::Max(Entry.Lifetime, 0.1f);
    const float NormalisedTime = FMath::Clamp(Entry.Elapsed / Lifetime, 0.f, 1.f);

    const float VerticalArc = FMath::Sin(NormalisedTime * PI) * FloaterArcHeight;
    const float HorizontalDrift = Entry.HorizontalDirection * FloaterHorizontalDrift *
                                 NormalisedTime;
    const FVector2D Offset = Entry.InitialOffset +
                             FVector2D(HorizontalDrift, -VerticalArc);

    const bool bVisible = Floater->UpdateProjection(Entry.AnchorLocation, Offset,
                                                    FloaterClampMargin);
    if (!bVisible) {
      // Hidden when occluded but we continue updating so it reappears if needed.
    }

    const float FadeStart = FMath::Max(Lifetime - Entry.FadeDuration, 0.f);
    float Opacity = 1.f;
    if (Entry.Elapsed >= FadeStart && Entry.FadeDuration > KINDA_SMALL_NUMBER) {
      Opacity = FMath::Clamp((Lifetime - Entry.Elapsed) / Entry.FadeDuration,
                             0.f, 1.f);
    }
    Floater->SetFloaterOpacity(Opacity);
    Floater->SetFloaterScale(Entry.Scale);

    if (Entry.Elapsed >= Lifetime) {
      ReleaseFloaterAtIndex(Index);
    }
  }
}

void USkaldMainHUDWidget::ReleaseFloaterAtIndex(int32 Index) {
  if (!ActiveFloaters.IsValidIndex(Index)) {
    return;
  }

  if (UW_FloatingText *Floater = ActiveFloaters[Index].Floater.Get()) {
    if (UCombatFloaterPoolSubsystem *Pool = ResolveFloaterPool()) {
      Pool->ReleaseFloater(Floater);
    } else {
      Floater->RemoveFromParent();
    }
  }

  ActiveFloaters.RemoveAtSwap(Index);
}

UCombatFloaterPoolSubsystem *USkaldMainHUDWidget::ResolveFloaterPool() {
  if (CachedFloaterPool.IsValid()) {
    return CachedFloaterPool.Get();
  }

  if (UWorld *World = GetWorld()) {
    if (UCombatFloaterPoolSubsystem *Pool =
            World->GetSubsystem<UCombatFloaterPoolSubsystem>()) {
      CachedFloaterPool = Pool;
      return Pool;
    }
  }

  return nullptr;
}

void USkaldMainHUDWidget::SetDefaultDiceResolutionPanelLayout(
    const FDiceResolutionPanelLayout &Layout) {
  DefaultDiceResolutionPanelLayout = Layout;
  ApplyDiceResolutionPanelLayoutInternal(DefaultDiceResolutionPanelLayout);
}

void USkaldMainHUDWidget::ApplyDiceResolutionPanelLayout(
    const FDiceResolutionPanelLayout &Layout) {
  ApplyDiceResolutionPanelLayoutInternal(Layout);
}

FDiceResolutionPanelLayout
USkaldMainHUDWidget::ResolveDiceResolutionPanelLayout_Implementation(
    AFighterPawn *Attacker, AFighterPawn *Defender,
    const FDiceRollResult &Result) const {
  return DefaultDiceResolutionPanelLayout;
}

void USkaldMainHUDWidget::ApplyDiceResolutionPanelLayoutInternal(
    const FDiceResolutionPanelLayout &Layout) {
  if (!DiceResolutionPanel || !Layout.bApplyLayout) {
    return;
  }

  if (UPanelSlot *PanelSlot = DiceResolutionPanel->Slot) {
    if (UCanvasPanelSlot *CanvasSlot = Cast<UCanvasPanelSlot>(PanelSlot)) {
      if (Layout.bOverrideAnchors) {
        CanvasSlot->SetAnchors(Layout.Anchors);
      }

      if (Layout.bOverrideAlignment) {
        CanvasSlot->SetAlignment(Layout.Alignment);
      }

      if (Layout.bOverridePosition) {
        CanvasSlot->SetPosition(Layout.Position);
      }

      if (Layout.bOverrideSize) {
        CanvasSlot->SetSize(Layout.Size);
      }
    }
  }
}
