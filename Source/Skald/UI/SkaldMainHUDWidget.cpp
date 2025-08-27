#include "UI/SkaldMainHUDWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Skald_GameMode.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "UI/ConfirmAttackWidget.h"
#include "WorldMap.h"

void USkaldMainHUDWidget::NativeConstruct() {
  Super::NativeConstruct();

  if (AttackButton) {
    AttackButton->OnClicked.AddDynamic(
        this, &USkaldMainHUDWidget::BeginAttackSelection);
  }
  if (MoveButton) {
    MoveButton->OnClicked.AddDynamic(this,
                                     &USkaldMainHUDWidget::BeginMoveSelection);
  }
  if (EndTurnButton) {
    EndTurnButton->OnClicked.AddDynamic(
        this, &USkaldMainHUDWidget::HandleEndTurnClicked);
  }
  if (DeployButton) {
    DeployButton->OnClicked.AddDynamic(
        this, &USkaldMainHUDWidget::HandleDeployClicked);
    DeployButton->SetVisibility(ESlateVisibility::Collapsed);
  }

  BP_SetPhaseButtons(CurrentPhase, CurrentPlayerID == LocalPlayerID);
  RebuildPlayerList(CachedPlayers);
}

void USkaldMainHUDWidget::HandleEndTurnClicked() {
  ShowEndingTurn();
  if (CurrentPhase == ETurnPhase::Attack) {
    OnEndAttackRequested.Broadcast(true);
  } else if (CurrentPhase == ETurnPhase::Movement) {
    OnEndMovementRequested.Broadcast(true);
  }

  if (APlayerController *PC = GetOwningPlayer()) {
    if (ASkaldPlayerController *SPC = Cast<ASkaldPlayerController>(PC)) {
      SPC->EndTurn();
    }
  }
}

void USkaldMainHUDWidget::UpdateTurnBanner(int32 InCurrentPlayerID,
                                           int32 InTurnNumber) {
  CurrentPlayerID = InCurrentPlayerID;
  TurnNumber = InTurnNumber;

  BP_SetTurnText(TurnNumber, CurrentPlayerID);
  BP_SetPhaseButtons(CurrentPhase, CurrentPlayerID == LocalPlayerID);
}

void USkaldMainHUDWidget::UpdatePhaseBanner(ETurnPhase InPhase) {
  CurrentPhase = InPhase;

  BP_SetPhaseText(CurrentPhase);
  BP_SetPhaseButtons(CurrentPhase, CurrentPlayerID == LocalPlayerID);

  // Toggle phase-specific widgets.
  if (AttackButton) {
    AttackButton->SetIsEnabled(CurrentPhase == ETurnPhase::Attack);
  }
  if (DeployButton) {
    const ESlateVisibility ButtonVisibility =
        CurrentPhase == ETurnPhase::Reinforcement
            ? DeployButton->GetVisibility()
            : ESlateVisibility::Collapsed;
    DeployButton->SetVisibility(ButtonVisibility);
  }
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
  BP_SetPhaseButtons(CurrentPhase, CurrentPlayerID == LocalPlayerID);
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
  if (EndTurnButton) {
    EndTurnButton->SetVisibility(bIsMyTurn ? ESlateVisibility::Visible
                                           : ESlateVisibility::Collapsed);
  }
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
      if (bOwnedByLocal) {
        SelectedSourceID = Territory->TerritoryID;

        if (AWorldMap *WorldMap =
                Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
                    GetWorld(), AWorldMap::StaticClass()))) {
          if (ATerritory *Source =
                  WorldMap->GetTerritoryById(SelectedSourceID)) {
            for (ATerritory *Terr : WorldMap->Territories) {
              if (Terr && Source->IsAdjacentTo(Terr) &&
                  Terr->OwningPlayer != Source->OwningPlayer) {
                Terr->Select();
                HighlightedTerritories.Add(Terr);
              }
            }
          }
        }
      }
    } else if (SelectedTargetID == -1) {
      SelectedTargetID = Territory->TerritoryID;

      if (SelectionPrompt) {
        SelectionPrompt->SetVisibility(ESlateVisibility::Collapsed);
      }
      if (ConfirmAttackWidgetClass) {
        ActiveConfirmWidget = CreateWidget<UConfirmAttackWidget>(
            GetWorld(), ConfirmAttackWidgetClass);
        if (ActiveConfirmWidget) {
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
      if (bOwnedByLocal && Territory->ArmyStrength > 0) {
        SelectedSourceID = Territory->TerritoryID;
        Territory->Select(true);
        if (SelectionPrompt) {
          SelectionPrompt->SetText(
              FText::FromString(TEXT("Choose enemy territory.")));
        }
      }
    } else if (SelectedTargetID == -1) {
      SelectedTargetID = Territory->TerritoryID;
    }
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

void USkaldMainHUDWidget::SyncPhaseButtons(bool bIsMyTurn) {
  BP_SetPhaseButtons(CurrentPhase, bIsMyTurn);
}

void USkaldMainHUDWidget::HandleAttackApproved() {
  const int32 ArmyCount =
      ActiveConfirmWidget ? ActiveConfirmWidget->ArmyCount : 0;
  SubmitAttack(SelectedSourceID, SelectedTargetID, ArmyCount);
}

void USkaldMainHUDWidget::HandleDeployClicked() {
  APlayerController *PC = GetOwningPlayer();
  if (!PC) {
    return;
  }

  ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>();
  if (!PS || PS->ArmyPool <= 0 || SelectedSourceID == -1) {
    return;
  }

  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    if (ATerritory *Terr = WorldMap->GetTerritoryById(SelectedSourceID)) {
      if (Terr->OwningPlayer == PS) {
        ++Terr->ArmyStrength;
        Terr->RefreshAppearance();
        --PS->ArmyPool;
        PS->ForceNetUpdate();

        if (ASkaldPlayerController *SKPC = Cast<ASkaldPlayerController>(PC)) {
          if (ATurnManager *TM = SKPC->GetTurnManager()) {
            TM->BroadcastArmyPool(PS);
          }
        }

        if (PS->ArmyPool <= 0 && DeployButton) {
          DeployButton->SetVisibility(ESlateVisibility::Collapsed);
          bool bHandled = false;
          if (ASkaldGameMode *GM =
                  GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
            if (!GM->HasMatchStarted()) {
              GM->AdvanceArmyPlacement();
              bHandled = true;
            }
          }
          if (!bHandled) {
            if (ASkaldPlayerController *SKPC =
                    Cast<ASkaldPlayerController>(PC)) {
              if (ATurnManager *TM = SKPC->GetTurnManager()) {
                TM->BeginAttackPhase();
              }
            }
          }
        }
      }
    }
  }
}
