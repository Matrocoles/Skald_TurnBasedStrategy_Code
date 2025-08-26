#include "UI/SkaldMainHUDWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

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

  BP_SetPhaseButtons(CurrentPhase, CurrentPlayerID == LocalPlayerID);
  RebuildPlayerList(CachedPlayers);
}

void USkaldMainHUDWidget::HandleEndTurnClicked() {
  if (CurrentPhase == ETurnPhase::Attack) {
    OnEndAttackRequested.Broadcast(true);
  } else if (CurrentPhase == ETurnPhase::Movement) {
    OnEndMovementRequested.Broadcast(true);
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

void USkaldMainHUDWidget::BeginAttackSelection() {
  bSelectingForAttack = true;
  bSelectingForMove = false;
  SelectedSourceID = -1;
  SelectedTargetID = -1;
}

void USkaldMainHUDWidget::SubmitAttack(int32 FromID, int32 ToID,
                                       int32 ArmySent) {
  OnAttackRequested.Broadcast(FromID, ToID, ArmySent);
  bSelectingForAttack = false;
  SelectedSourceID = -1;
  SelectedTargetID = -1;
}

void USkaldMainHUDWidget::CancelAttackSelection() {
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

void USkaldMainHUDWidget::OnTerritoryClickedUI(int32 TerritoryID,
                                               bool bOwnedByLocal) {
  if (bSelectingForAttack) {
    if (SelectedSourceID == -1) {
      if (bOwnedByLocal) {
        SelectedSourceID = TerritoryID;
      }
    } else if (SelectedTargetID == -1) {
      SelectedTargetID = TerritoryID;
    }
  } else if (bSelectingForMove) {
    if (SelectedSourceID == -1) {
      if (bOwnedByLocal) {
        SelectedSourceID = TerritoryID;
      }
    } else if (SelectedTargetID == -1) {
      if (bOwnedByLocal) {
        SelectedTargetID = TerritoryID;
      }
    }
  }
}

void USkaldMainHUDWidget::SyncPhaseButtons(bool bIsMyTurn) {
  BP_SetPhaseButtons(CurrentPhase, bIsMyTurn);
}
