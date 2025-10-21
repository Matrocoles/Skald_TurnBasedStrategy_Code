#include "UI/SkaldMainHUDWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/Widget.h"
#include "Containers/Queue.h"
#include "Containers/Set.h"
#include "FighterPawn.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/Engine.h"
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
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "UI/ConfirmAttackWidget.h"
#include "UI/DeployWidget.h"
#include "UI/CombatFloaterPoolSubsystem.h"
#include "UI/W_FloatingText.h"
#include "UI/W_DiceResolutionPanel.h"
#include "UI/SkaldUIHelpers.h"
#include "UObject/ConstructorHelpers.h"
#include "WorldMap.h"
#include "TimerManager.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Math/UnrealMathUtility.h"

USkaldMainHUDWidget::USkaldMainHUDWidget(
    const FObjectInitializer &ObjectInitializer)
    : Super(ObjectInitializer) {
  FloaterWidgetClass = UW_FloatingText::StaticClass();

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
}

void USkaldMainHUDWidget::NativeConstruct() {
  Super::NativeConstruct();

  SetIsFocusable(true);
  SetFocus();

  // Ensure the full-screen HUD doesn't swallow world clicks.
  if (UWidget *Root = GetRootWidget()) {
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
  }

  GameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
  if (!GameMode) {
    UE_LOG(LogSkald, Warning,
           TEXT("SkaldMainHUDWidget could not find GameMode."));
  }
  GameState = GetWorld()->GetGameState<ASkaldGameState>();
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
}

void USkaldMainHUDWidget::NativeTick(const FGeometry &MyGeometry,
                                     float InDeltaTime) {
  Super::NativeTick(MyGeometry, InDeltaTime);
  UpdateActiveFloaters(InDeltaTime);
}

void USkaldMainHUDWidget::NativeDestruct() {
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

  const FLinearColor PlayerColor = FLinearColor::Green;
  const FLinearColor EnemyColor = FLinearColor::Red;
  EndingTurnText->SetColorAndOpacity(bIsPlayerMessage ? PlayerColor : EnemyColor);
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
      ShowSelectionPromptMessage(TEXT("Select an owned capital."));
    } else if (CurrentPhase == ETurnPhase::ArmyPlacement) {
      ShowSelectionPromptMessage(TEXT("Select an owned territory."));
    } else if (CurrentPhase == ETurnPhase::Movement) {
      ShowSelectionPromptMessage(
          TEXT("Press Move, then select an owned territory."));
    } else if (CurrentPhase == ETurnPhase::Attack) {
      ShowSelectionPromptMessage(
          TEXT("Press Attack, then select an owned territory."));
    } else {
      ShowSelectionPromptMessage(TEXT(""), false);
    }
  }
}

void USkaldMainHUDWidget::UpdateTerritoryInfo(const FString &TerritoryName,
                                              const FString &OwnerName,
                                              int32 ArmyCount) {
  BP_SetTerritoryPanel(TerritoryName, OwnerName, ArmyCount);

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
    bool bOwnedCapitalByLocal = false;
    if (APlayerController *PC = GetOwningPlayer()) {
      if (ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>()) {
        if (AWorldMap *Map = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
                GetWorld(), AWorldMap::StaticClass()))) {
          if (ATerritory *Sel = Map->SelectedTerritory) {
            const bool bOwnedByLocal =
                (Sel->OwningPlayer &&
                 Sel->OwningPlayer->GetPlayerId() == PS->GetPlayerId());
            bOwnedCapitalByLocal = bOwnedByLocal && Sel->bIsCapital;
          }
        }
      }
    }
    const bool bShouldShowDeploy = bIsMyTurn && bOwnedCapitalByLocal;
    DeployButton->SetVisibility(bShouldShowDeploy ? ESlateVisibility::Visible
                                                  : ESlateVisibility::Collapsed);
    DeployButton->SetIsEnabled(bShouldShowDeploy);
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

  PlayerListBox->ClearChildren();

  UEnum *FactionEnum = StaticEnum<ESkaldFaction>();

  for (const FS_PlayerData &Player : Players) {
    if (Player.IsEliminated) {
      continue;
    }
    UTextBlock *Entry = NewObject<UTextBlock>(PlayerListBox);
    if (!Entry) {
      continue;
    }

    FString FactionName = FactionEnum
                              ? FactionEnum
                                    ->GetDisplayNameTextByValue(
                                        static_cast<int64>(Player.Faction))
                                    .ToString()
                              : TEXT("Unknown");
    FString Line =
        FString::Printf(TEXT("%s (%s)%s"), *Player.PlayerName, *FactionName,
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
}

void USkaldMainHUDWidget::HideEnemyTurnInProgress() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(TurnMessageTimerHandle);
  }
  HideEndingTurn();
}

void USkaldMainHUDWidget::ShowStrategicInitiativePrompt(const FText &PromptText,
                                                        float ButtonDelay) {
  SetAwaitingStrategicInitiative(true);

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

void USkaldMainHUDWidget::ShowStrategicInitiativeRoll(int32 RollValue,
                                                      float DisplayDuration) {
  if (!InitiativeDiceImage) {
    return;
  }

  if (RollValue <= 0) {
    HideStrategicInitiativeDice();
    return;
  }

  SetAwaitingStrategicInitiative(false);

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

  const float DiceDisplaySize = 90.f;
  const float DiceBoardPadding = 24.f;
  const FVector2D DiceOffset(0.f, 80.f);

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
      DiceSlot->SetAnchors(FAnchors(0.5f, 0.f));
      DiceSlot->SetAlignment(FVector2D(0.5f, 0.f));
      DiceSlot->SetPosition(DiceOffset);
      DiceSlot->SetSize(FVector2D(DiceDisplaySize, DiceDisplaySize));
    }

    InitiativeDiceImage->SetVisibility(ESlateVisibility::HitTestInvisible);

    if (InitiativeDiceBoardImage) {
      InitiativeDiceBoardImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

      if (UCanvasPanelSlot *BoardSlot =
              Cast<UCanvasPanelSlot>(InitiativeDiceBoardImage->Slot)) {
        BoardSlot->SetAnchors(FAnchors(0.5f, 0.f));
        BoardSlot->SetAlignment(FVector2D(0.5f, 0.f));
        BoardSlot->SetPosition(
            DiceOffset - FVector2D(0.f, DiceBoardPadding * 0.5f));
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

  const bool bIsMyTurn = CurrentPlayerID == LocalPlayerID;
  SyncPhaseButtons(bIsMyTurn);
}

void USkaldMainHUDWidget::ClearStrategicInitiativeWaitIfNeeded() {
  if (bAwaitingStrategicInitiative) {
    SetAwaitingStrategicInitiative(false);
  }
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
                                              const FDiceRollResult &Result) {
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

void USkaldMainHUDWidget::ShowSelectionPromptMessage(const FString &Message,
                                                     bool bShow) {
  if (!SelectionPrompt) {
    return;
  }

  SelectionPrompt->SetText(FText::FromString(Message));
  SelectionPrompt->SetVisibility(bShow ? ESlateVisibility::Visible
                                       : ESlateVisibility::Collapsed);
}

void USkaldMainHUDWidget::ShowSelectionErrorMessage(const FString &Message) {
  ShowSelectionPromptMessage(Message);
  ShowErrorMessage(Message);
}

ATerritory *USkaldMainHUDWidget::GetCurrentlySelectedTerritory() const {
  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    return WorldMap->SelectedTerritory;
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

  ShowSelectionPromptMessage(TEXT("Select an owned territory to attack from."));

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
  if (CurrentPhase == ETurnPhase::Reinforcement) {
    ShowSelectionPromptMessage(TEXT("Select an owned capital."));
  } else if (CurrentPhase == ETurnPhase::ArmyPlacement) {
    ShowSelectionPromptMessage(TEXT("Select an owned territory."));
  } else {
    ShowSelectionPromptMessage(
        TEXT("Press Attack, then select an owned territory."));
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
  ShowSelectionPromptMessage(TEXT("Select a territory to move troops from."));

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
    ShowSelectionPromptMessage(Message);
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
    ShowSelectionPromptMessage(
        TEXT("Press Move, then select an owned territory."));
  } else {
    ShowSelectionPromptMessage(TEXT(""), false);
  }
}

void USkaldMainHUDWidget::ResetMoveSelectionAfterInvalidAttempt() {
  ClearTerritoryHighlights();
  SelectedSourceID = -1;
  SelectedTargetID = -1;
  if (CurrentPhase == ETurnPhase::Movement) {
    bSelectingForMove = true;
    bSelectingForAttack = false;
    ShowSelectionPromptMessage(TEXT("Select a territory to move troops from."));
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

  const bool bOwnedByLocal =
      LocalPS && IsValid(Territory->OwningPlayer) &&
      Territory->OwningPlayer->GetPlayerId() == LocalPS->GetPlayerId();

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
        if (SelectionPrompt) {
          SelectionPrompt->SetText(
              FText::FromString(TEXT("Choose enemy territory.")));
        }
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
      if (SelectionPrompt) {
        SelectionPrompt->SetVisibility(ESlateVisibility::Collapsed);
      }
      if (ConfirmAttackWidgetClass) {
        ActiveConfirmWidget = CreateWidget<UConfirmAttackWidget>(
            GetWorld(), ConfirmAttackWidgetClass);
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
      ShowSelectionErrorMessage(
          TEXT("You may only move between your own territories."));
      return;
    }

    if (SelectedSourceID == -1 || Territory->TerritoryID == SelectedSourceID) {
      if (Territory->ArmyUnits <= 1) {
        ShowSelectionErrorMessage(
            TEXT("Need more than one unit to move troops."));
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
        ShowSelectionErrorMessage(
            TEXT("No connected friendly territory to move into."));
        SelectedSourceID = -1;
        Territory->Deselect();
        return;
      }

      ShowSelectionPromptMessage(
          TEXT("Select a connected territory to receive troops."));
      return;
    }

    const bool bIsHighlighted = HighlightedTerritories.ContainsByPredicate(
        [Territory](ATerritory *T) {
          return T && T->TerritoryID == Territory->TerritoryID;
        });

    if (!bIsHighlighted) {
      ShowSelectionErrorMessage(TEXT("Target not valid for movement."));
      return;
    }

    SelectedTargetID = Territory->TerritoryID;

    AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
        GetWorld(), AWorldMap::StaticClass()));
    if (!WorldMap) {
      ShowSelectionErrorMessage(TEXT("World map not found."));
      CancelMoveSelection();
      return;
    }

    ATerritory *Source = WorldMap->GetTerritoryById(SelectedSourceID);
    ATerritory *Target = WorldMap->GetTerritoryById(SelectedTargetID);
    if (!Source || !Target) {
      ShowSelectionErrorMessage(TEXT("Invalid territory selection."));
      CancelMoveSelection();
      return;
    }

    const int32 MaxMovable = Source->ArmyUnits - 1;
    if (MaxMovable <= 0) {
      ShowSelectionErrorMessage(TEXT("No troops available to move."));
      CancelMoveSelection();
      return;
    }

    if (!DeployWidgetClass) {
      ShowSelectionErrorMessage(TEXT("Deploy widget unavailable."));
      CancelMoveSelection();
      return;
    }

    if (ActiveDeployWidget && ActiveDeployWidget->IsInViewport()) {
      ActiveDeployWidget->RemoveFromParent();
      ActiveDeployWidget = nullptr;
    }

    ActiveDeployWidget =
        CreateWidget<UDeployWidget>(GetWorld(), DeployWidgetClass);
    if (!ActiveDeployWidget) {
      ShowSelectionErrorMessage(TEXT("Could not open deploy UI."));
      CancelMoveSelection();
      return;
    }

    ActiveDeployWidget->SetupTransfer(Source, Target, this, MaxMovable);
    ActiveDeployWidget->AddToViewport();
    if (APlayerController *FocusPC = GetOwningPlayer()) {
      FocusWidgetUIOnly(FocusPC, ActiveDeployWidget);
    }

    ShowSelectionPromptMessage(TEXT("Choose how many troops to move."));
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
      const bool bShouldShowDeploy = bIsMyTurn && bOwnedByLocal && bIsCapital;
      DeployButton->SetVisibility(
          bShouldShowDeploy ? ESlateVisibility::Visible
                            : ESlateVisibility::Collapsed);
      DeployButton->SetIsEnabled(bShouldShowDeploy);
    }
    if (CurrentPhase == ETurnPhase::Reinforcement) {
      if (bOwnedByLocal && !Territory->bIsCapital) {
        ShowSelectionErrorMessage(
            TEXT("Reinforcements can only be placed on owned capitals."));
      } else if (bOwnedByLocal) {
        ShowSelectionPromptMessage(TEXT("Select an owned capital."));
      }
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
      return LocalPS->GetPlayerId();
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
      Data.PlayerID = PS->GetPlayerId();
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
  const int32 PreviousTurnNumber = TurnNumber;

  // Derive current player ID and approximate turn number from GameState.
  int32 NewPlayerID = -1;
  int32 NewTurnNumber = TurnNumber;
  if (GameState) {
    if (ASkaldPlayerState *PS = GameState->GetCurrentPlayer()) {
      NewPlayerID = PS->GetPlayerId();
    }

    const int32 PlayerCount = GameState->PlayerArray.Num();
    if (PlayerCount > 0) {
      NewTurnNumber = GameState->CurrentTurnIndex / PlayerCount + 1;
    }
  }

  const bool bIsNewRound = (NewTurnNumber != PreviousTurnNumber);

  // Update cached turn state before refreshing widgets.
  CurrentPlayerID = NewPlayerID;
  TurnNumber = NewTurnNumber;
  ClearStrategicInitiativeWaitIfNeeded();

  if (bIsNewRound && RoundStartSound) {
    UGameplayStatics::PlaySound2D(this, RoundStartSound);
  }

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

  const bool bShowEndPhaseButton =
      bIsMyTurn && CurrentPhase != ETurnPhase::EndTurn;
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
    if (SelectionPrompt) {
      SelectionPrompt->SetText(FText::FromString(Error));
      SelectionPrompt->SetVisibility(ESlateVisibility::Visible);
    }
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, Error);
    }
    return;
  }

  // Remove the confirmation widget now that the attack is approved
  ActiveConfirmWidget->RemoveFromParent();
  ActiveConfirmWidget = nullptr;

  SubmitAttack(SourceID, TargetID, ArmyCount, bUseSiegeForNextAttack);

  const bool bUseSiege = bUseSiegeForNextAttack;
  bUseSiegeForNextAttack = false;

  // Trigger the battle immediately
  if (APlayerController *PC = GetOwningPlayer()) {
    if (ASkaldPlayerController *SPC = Cast<ASkaldPlayerController>(PC)) {
      if (ATurnManager *TM = SPC->GetTurnManager()) {
        FS_BattlePayload Battle;
        Battle.FromTerritoryID = SourceID;
        Battle.TargetTerritoryID = TargetID;
        Battle.ArmyCountSent = ArmyCount;
        if (AWorldMap *WorldMap =
                Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
                    GetWorld(), AWorldMap::StaticClass()))) {
          if (ATerritory *Source = WorldMap->GetTerritoryById(SourceID)) {
            if (IsValid(Source->OwningPlayer)) {
              Battle.AttackerPlayerID = Source->OwningPlayer->GetPlayerId();
              Battle.AttackerFaction = Source->OwningPlayer->Faction;
              Battle.AttackerDisplayName =
                  Source->OwningPlayer->GetResolvedPlayerName(
                      TEXT("HUD_Attack_Attacker"));
              Battle.bAttackerIsAI = Source->OwningPlayer->bIsAI;
            }
            if (bUseSiege && Source->BuiltSiegeID > 0) {
              Battle.AssignedSiegeIDs.Add(Source->BuiltSiegeID);
              Source->BuiltSiegeID = 0;
            }
          }
          if (ATerritory *Target = WorldMap->GetTerritoryById(TargetID)) {
            if (IsValid(Target->OwningPlayer)) {
              Battle.DefenderPlayerID = Target->OwningPlayer->GetPlayerId();
              Battle.DefenderFaction = Target->OwningPlayer->Faction;
              Battle.DefenderDisplayName =
                  Target->OwningPlayer->GetResolvedPlayerName(
                      TEXT("HUD_Attack_Defender"));
              Battle.bDefenderIsAI = Target->OwningPlayer->bIsAI;
            }
            Battle.IsCapitalAttack = Target->bIsCapital;
          }
        }
        TM->TriggerGridBattle(Battle);
      }
    }
  }
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
      Territory->OwningPlayer->GetPlayerId() != PS->GetPlayerId()) {
    UE_LOG(LogSkald, Warning,
           TEXT("HandleDeployClicked failed: invalid territory %d"),
           SelectedSourceID);
    ShowErrorMessage(TEXT("Invalid territory selected"));
    return;
  }

  if (!Territory->bIsCapital) {
    ShowSelectionErrorMessage(
        TEXT("Reinforcements can only be placed on owned capitals."));
    return;
  }

  ActiveDeployWidget =
      CreateWidget<UDeployWidget>(GetWorld(), DeployWidgetClass);
  if (ActiveDeployWidget) {
    ActiveDeployWidget->SetupDeployment(Territory, PS, this,
                                        PS->DeployableUnits);
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
