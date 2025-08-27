#include "UI/SkaldMainHUDWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Skald_GameInstance.h"
#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "UI/ConfirmAttackWidget.h"
#include "UI/DeployWidget.h"
#include "WorldMap.h"

void USkaldMainHUDWidget::NativeConstruct() {
  Super::NativeConstruct();

  GameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
  if (!GameMode) {
    UE_LOG(LogTemp, Warning,
           TEXT("SkaldMainHUDWidget could not find GameMode."));
  }
  GameState = GetWorld()->GetGameState<ASkaldGameState>();
  if (!GameState) {
    UE_LOG(LogTemp, Warning,
           TEXT("SkaldMainHUDWidget could not find GameState."));
  } else {
    GameState->OnPlayersUpdated.AddDynamic(
        this, &USkaldMainHUDWidget::HandlePlayersUpdated);
  }
  GameInstance = GetGameInstance<USkaldGameInstance>();
  if (!GameInstance) {
    UE_LOG(LogTemp, Warning,
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
  SyncPhaseButtons(CurrentPlayerID == LocalPlayerID);

  if (DeployableUnitsText && CurrentPhase != ETurnPhase::Reinforcement) {
    DeployableUnitsText->SetVisibility(ESlateVisibility::Collapsed);
  }
}

void USkaldMainHUDWidget::UpdateTerritoryInfo(const FString &TerritoryName,
                                              const FString &OwnerName,
                                              int32 ArmyCount) {
  BP_SetTerritoryPanel(TerritoryName, OwnerName, ArmyCount);
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

void USkaldMainHUDWidget::SubmitAttack(int32 FromID, int32 ToID,
                                       int32 ArmySent) {
  OnAttackRequested.Broadcast(FromID, ToID, ArmySent);
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

  const bool bOwnedByLocal = LocalPS && Territory->OwningPlayer == LocalPS;

  if (bSelectingForAttack) {
    if (SelectedSourceID == -1) {
      if (bOwnedByLocal && Territory->ArmyStrength > 0) {
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
      }
      return;
    }

    // Source selected: only allow choosing highlighted enemy territories
    if (HighlightedTerritories.Contains(Territory)) {
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
              MaxUnits = Source->ArmyStrength;
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
        }
      }
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
  } else if (CurrentPhase == ETurnPhase::Reinforcement && bOwnedByLocal) {
    SelectedSourceID = Territory->TerritoryID;
    if (DeployButton) {
      DeployButton->SetVisibility(ESlateVisibility::Visible);
    }
  }
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
      Data.PlayerName = PS->DisplayName;
      Data.IsAI = PS->bIsAI;
      Data.Faction = PS->Faction;
      Players.Add(Data);
    }
  }

  RefreshPlayerList(Players);
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
        (bIsMyTurn && CurrentPhase == ETurnPhase::Reinforcement)
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

  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    if (ATerritory *Source = WorldMap->GetTerritoryById(SourceID)) {
      ArmyCount = FMath::Clamp(ArmyCount, 1, Source->ArmyStrength);
    }
  }

  SubmitAttack(SourceID, TargetID, ArmyCount);

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
    return;
  }

  ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>();
  if (!PS || PS->ArmyPool <= 0 || SelectedSourceID == -1 ||
      !DeployWidgetClass) {
    return;
  }

  ATerritory *Territory = nullptr;
  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    Territory = WorldMap->GetTerritoryById(SelectedSourceID);
  }

  if (!Territory || Territory->OwningPlayer != PS) {
    return;
  }

  ActiveDeployWidget =
      CreateWidget<UDeployWidget>(GetWorld(), DeployWidgetClass);
  if (ActiveDeployWidget) {
    ActiveDeployWidget->Setup(Territory, PS, this, PS->ArmyPool);
    ActiveDeployWidget->AddToViewport();
  }
}
