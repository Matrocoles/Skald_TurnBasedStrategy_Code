#include "UI/SkaldMainHUDWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Skald.h"
#include "SkaldTypes.h"
#include "Skald_GameInstance.h"
#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_AIController.h"
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "UI/ConfirmAttackWidget.h"
#include "UI/DeployWidget.h"
#include "WorldMap.h"
#include "UObject/ConstructorHelpers.h"

USkaldMainHUDWidget::USkaldMainHUDWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer) {
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
    UE_LOG(LogSkald, Error,
           TEXT("SkaldMainHUDWidget: failed to find confirm attack widget class"));
  }
}

void USkaldMainHUDWidget::NativeConstruct() {
  Super::NativeConstruct();

  // Ensure the full-screen HUD doesn't swallow world clicks.
  if (UWidget* Root = GetRootWidget()) {
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

  SyncPhaseButtons(CurrentPlayerID == LocalPlayerID);
  RebuildPlayerList(CachedPlayers);
}

void USkaldMainHUDWidget::NativeDestruct() {
  if (GameState) {
    GameState->OnPlayersUpdated.RemoveDynamic(
        this, &USkaldMainHUDWidget::HandlePlayersUpdated);
    GameState->OnTurnIndexChanged.RemoveDynamic(
        this, &USkaldMainHUDWidget::HandleTurnIndexChanged);
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

  Super::NativeDestruct();
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
  } else if (CurrentPhase == ETurnPhase::Movement) {
    OnEndMovementRequested.Broadcast(true);
  } else if (CurrentPhase == ETurnPhase::ArmyPlacement) {
    if (GameMode) {
      GameMode->AdvanceArmyPlacement();
    }
    return;
  }

  if (APlayerController *PC = GetOwningPlayer()) {
    if (ASkaldPlayerController *SPC = Cast<ASkaldPlayerController>(PC)) {
      SPC->EndPhase();
    }
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
  CurrentPhase = InPhase;

  BP_SetPhaseText(CurrentPhase);
  if (CurrentPhase != ETurnPhase::Attack) {
    CancelAttackSelection();
  }
  if (CurrentPhase != ETurnPhase::Reinforcement && CurrentPhase != ETurnPhase::ArmyPlacement) {
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
}

void USkaldMainHUDWidget::UpdateTerritoryInfo(const FString &TerritoryName,
                                              const FString &OwnerName,
                                              int32 ArmyCount) {
  BP_SetTerritoryPanel(TerritoryName, OwnerName, ArmyCount);

  // Keep Deploy button visibility in sync with current selection ownership.
  if (DeployButton && (CurrentPhase == ETurnPhase::Reinforcement ||
                       CurrentPhase == ETurnPhase::ArmyPlacement)) {
    bool bOwnedByLocal = false;
    if (APlayerController* PC = GetOwningPlayer()) {
      if (ASkaldPlayerState* PS = PC->GetPlayerState<ASkaldPlayerState>()) {
        if (AWorldMap* Map = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
                GetWorld(), AWorldMap::StaticClass()))) {
          if (ATerritory* Sel = Map->SelectedTerritory) {
            bOwnedByLocal = (Sel->OwningPlayer &&
                             Sel->OwningPlayer->GetPlayerId() == PS->GetPlayerId());
          }
        }
      }
    }
    DeployButton->SetVisibility(bOwnedByLocal ? ESlateVisibility::Visible
                                              : ESlateVisibility::Collapsed);
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
    EndingTurnText->SetText(FText::FromString(TEXT("Ending turn.")));
    EndingTurnText->SetVisibility(ESlateVisibility::Visible);
  }
}

void USkaldMainHUDWidget::HideEndingTurn() {
  if (EndingTurnText) {
    EndingTurnText->SetVisibility(ESlateVisibility::Collapsed);
  }
}

void USkaldMainHUDWidget::ShowTurnMessage(bool bIsMyTurn) {
  if (EndingTurnText) {
    EndingTurnText->SetText(
        FText::FromString(bIsMyTurn ? TEXT("Your turn") : TEXT("Enemy turn")));
    EndingTurnText->SetVisibility(ESlateVisibility::Visible);
  }
  SyncPhaseButtons(bIsMyTurn);
}

void USkaldMainHUDWidget::UpdateInitiativeText(const FString &Message) {
  if (InitiativeText) {
    InitiativeText->SetText(FText::FromString(Message));
    InitiativeText->SetVisibility(ESlateVisibility::Visible);
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

void USkaldMainHUDWidget::BeginAttackSelection() {
  bSelectingForAttack = true;
  bSelectingForMove = false;
  SelectedSourceID = -1;
  SelectedTargetID = -1;

  for (ATerritory *Terr : HighlightedTerritories) {
    if (Terr) {
      Terr->Deselect();
    }
  }
  HighlightedTerritories.Empty();

  if (ActiveConfirmWidget) {
    ActiveConfirmWidget->RemoveFromParent();
    ActiveConfirmWidget = nullptr;
  }

  if (SelectionPrompt) {
    SelectionPrompt->SetText(
        FText::FromString(TEXT("Choose owned territory.")));
    SelectionPrompt->SetVisibility(ESlateVisibility::Visible);
  }
}

void USkaldMainHUDWidget::SubmitAttack(int32 FromID, int32 ToID, int32 ArmySent,
                                       bool bUseSiege) {
  OnAttackRequested.Broadcast(FromID, ToID, ArmySent, bUseSiege);
  CancelAttackSelection();
}

void USkaldMainHUDWidget::CancelAttackSelection() {
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
    for (ATerritory *Terr : HighlightedTerritories) {
      if (Terr) {
        Terr->Deselect();
      }
    }
  }
  HighlightedTerritories.Empty();
  if (ActiveConfirmWidget) {
    ActiveConfirmWidget->RemoveFromParent();
    ActiveConfirmWidget = nullptr;
  }
  if (SelectionPrompt) {
    SelectionPrompt->SetVisibility(ESlateVisibility::Visible);
  }
  bSelectingForAttack = false;
  SelectedSourceID = -1;
  SelectedTargetID = -1;
}

void USkaldMainHUDWidget::BeginMoveSelection() {
  bSelectingForMove = true;
  bSelectingForAttack = false;
  SelectedSourceID = -1;
  SelectedTargetID = -1;
}

void USkaldMainHUDWidget::SubmitMove(int32 FromID, int32 ToID, int32 Troops) {
  OnMoveRequested.Broadcast(FromID, ToID, Troops);
  bSelectingForMove = false;
  SelectedSourceID = -1;
  SelectedTargetID = -1;
}

void USkaldMainHUDWidget::CancelMoveSelection() {
  bSelectingForMove = false;
  SelectedSourceID = -1;
  SelectedTargetID = -1;
}

void USkaldMainHUDWidget::OnTerritoryClickedUI(ATerritory *Territory) {
  if (!Territory) {
    return;
  }

  ASkaldPlayerState *LocalPS = nullptr;
  if (APlayerController *PC = GetOwningPlayer()) {
    LocalPS = PC->GetPlayerState<ASkaldPlayerState>();
  }

  const bool bOwnedByLocal = LocalPS && Territory->OwningPlayer &&
                             Territory->OwningPlayer->GetPlayerId() ==
                                 LocalPS->GetPlayerId();

  if (bSelectingForAttack) {
    // If player is mid-selection and clicks a different source,
    // clear previous highlights so we don't accumulate stale visuals.
    if (SelectedSourceID != -1 && SelectedTargetID == -1) {
      for (ATerritory* T : HighlightedTerritories) {
        if (T) {
          T->Deselect();
        }
      }
      HighlightedTerritories.Empty();
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
    const bool bIsHighlighted = HighlightedTerritories.ContainsByPredicate(
        [Territory](ATerritory* T) { return T && T->TerritoryID == Territory->TerritoryID; });
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
      UE_LOG(LogSkald, Warning,
             TEXT("OnTerritoryClickedUI: territory %d not highlighted for attack"),
             Territory->TerritoryID);
      ShowErrorMessage(TEXT("Target not attackable"));
    }
    return;
  } else if (bSelectingForMove) {
    if (SelectedSourceID == -1) {
      if (bOwnedByLocal) {
        SelectedSourceID = Territory->TerritoryID;
      }
    } else if (SelectedTargetID == -1) {
      if (bOwnedByLocal) {
        SelectedTargetID = Territory->TerritoryID;
      }
    }
  } else if (CurrentPhase == ETurnPhase::Reinforcement ||
             CurrentPhase == ETurnPhase::ArmyPlacement) {
    SelectedSourceID = Territory->TerritoryID;
    if (bOwnedByLocal && DeployButton) {
      DeployButton->SetVisibility(ESlateVisibility::Visible);
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

void USkaldMainHUDWidget::HandlePlayersUpdated() {
  if (!GameState) {
    return;
  }

  TArray<FS_PlayerData> Players;
  for (APlayerState *PSBase : GameState->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      FS_PlayerData Data;
      Data.PlayerID = PS->GetPlayerId();
      Data.PlayerName = PS->PlayerDisplayName;
      Data.IsAI = PS->bIsAI;
      Data.Faction = PS->Faction;
      Players.Add(Data);
    }
  }

  RefreshPlayerList(Players);
}

void USkaldMainHUDWidget::HandleTurnIndexChanged(int32 /*NewTurnIndex*/) {
  // Derive current player ID from GameState.
  int32 NewPlayerID = -1;
  if (GameState) {
    if (ASkaldPlayerState* PS = GameState->GetCurrentPlayer()) {
      NewPlayerID = PS->GetPlayerId();
    }
  }

  // Keep existing turn number; only the active player changed.
  CurrentPlayerID = NewPlayerID;

  // Update widget text and cached state.
  BP_SetTurnText(TurnNumber, CurrentPlayerID);

  // Update buttons for the new player and show a brief turn message.
  const bool bIsMyTurn = (CurrentPlayerID == LocalPlayerID);
  SyncPhaseButtons(bIsMyTurn);
  ShowTurnMessage(bIsMyTurn);
}

void USkaldMainHUDWidget::SyncPhaseButtons(bool bIsMyTurn) {
  BP_SetPhaseButtons(CurrentPhase, bIsMyTurn);

  if (AttackButton) {
    const ESlateVisibility DesiredVisibility =
        (bIsMyTurn && CurrentPhase == ETurnPhase::Attack)
            ? ESlateVisibility::Visible
            : ESlateVisibility::Collapsed;
    AttackButton->SetVisibility(DesiredVisibility);
  }

  if (MoveButton) {
    const ESlateVisibility DesiredVisibility =
        (bIsMyTurn && CurrentPhase == ETurnPhase::Movement)
            ? ESlateVisibility::Visible
            : ESlateVisibility::Collapsed;
    MoveButton->SetVisibility(DesiredVisibility);
  }

  if (DeployButton) {
    const ESlateVisibility DesiredVisibility =
        (bIsMyTurn && (CurrentPhase == ETurnPhase::Reinforcement ||
                        CurrentPhase == ETurnPhase::ArmyPlacement))
            ? ESlateVisibility::Visible
            : ESlateVisibility::Collapsed;
    DeployButton->SetVisibility(DesiredVisibility);
  }

  if (EndPhaseButton) {
    EndPhaseButton->SetVisibility(ESlateVisibility::Visible);
    EndPhaseButton->SetIsEnabled(bIsMyTurn);
  }

  if (EndTurnButton) {
    EndTurnButton->SetVisibility(ESlateVisibility::Visible);
    EndTurnButton->SetIsEnabled(bIsMyTurn);
  }
}

void USkaldMainHUDWidget::HandleAttackApproved() {
  if (!ActiveConfirmWidget) {
    return;
  }

  const int32 SourceID = SelectedSourceID;
  const int32 TargetID = SelectedTargetID;
  int32 ArmyCount = ActiveConfirmWidget->ArmyCount;
  bool bTargetIsCapital = false;

  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    if (ATerritory *Source = WorldMap->GetTerritoryById(SourceID)) {
      ArmyCount = FMath::Clamp(ArmyCount, 1, Source->ArmyUnits);
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
            if (Source->OwningPlayer) {
              Battle.AttackerPlayerID = Source->OwningPlayer->GetPlayerId();
            }
            if (bUseSiege && Source->BuiltSiegeID > 0) {
              Battle.AssignedSiegeIDs.Add(Source->BuiltSiegeID);
              Source->BuiltSiegeID = 0;
            }
          }
          if (ATerritory *Target = WorldMap->GetTerritoryById(TargetID)) {
            if (Target->OwningPlayer) {
              Battle.DefenderPlayerID = Target->OwningPlayer->GetPlayerId();
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

  ActiveDeployWidget =
      CreateWidget<UDeployWidget>(GetWorld(), DeployWidgetClass);
  if (ActiveDeployWidget) {
    ActiveDeployWidget->Setup(Territory, PS, this, PS->DeployableUnits);
    ActiveDeployWidget->AddToViewport();
  } else {
    UE_LOG(LogSkald, Warning,
           TEXT("HandleDeployClicked failed: could not create widget"));
    ShowErrorMessage(TEXT("Could not open deploy UI"));
  }
}

void USkaldMainHUDWidget::ClearDeployWidget() {
  if (ActiveDeployWidget) {
    ActiveDeployWidget->RemoveFromParent();
    ActiveDeployWidget = nullptr;
  }
}

