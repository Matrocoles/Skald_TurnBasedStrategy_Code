#include "UI/PrepareForBattleWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

void UPrepareForBattleWidget::NativeOnInitialized() {
  Super::NativeOnInitialized();

  if (PrepareForBattleButton &&
      !PrepareForBattleButton->OnClicked.IsAlreadyBound(
          this, &UPrepareForBattleWidget::HandlePrepareButtonClicked)) {
    PrepareForBattleButton->OnClicked.AddDynamic(
        this, &UPrepareForBattleWidget::HandlePrepareButtonClicked);
  }

  RefreshTextWidgets();
}

void UPrepareForBattleWidget::SynchronizeProperties() {
  Super::SynchronizeProperties();
  RefreshTextWidgets();
}

void UPrepareForBattleWidget::SetupBattleDetails(
    const FText &InAttackingPlayerID, const FText &InAttackingTerritoryID,
    const FText &InDefendingPlayerID, const FText &InDefendingTerritoryID) {
  AttackingPlayerIDText = InAttackingPlayerID;
  AttackingTerritoryIDText = InAttackingTerritoryID;
  DefendingPlayerIDText = InDefendingPlayerID;
  DefendingTerritoryIDText = InDefendingTerritoryID;
  RefreshTextWidgets();
}

void UPrepareForBattleWidget::HandlePrepareButtonClicked() {
  OnPrepareButtonClicked.Broadcast();
}

void UPrepareForBattleWidget::RefreshTextWidgets() {
  if (AttackingPlayerID) {
    AttackingPlayerID->SetText(AttackingPlayerIDText);
  }
  if (AttackingTerritoryID) {
    AttackingTerritoryID->SetText(AttackingTerritoryIDText);
  }
  if (DefendingPlayerID) {
    DefendingPlayerID->SetText(DefendingPlayerIDText);
  }
  if (DefendingTerritoryID) {
    DefendingTerritoryID->SetText(DefendingTerritoryIDText);
  }
}

