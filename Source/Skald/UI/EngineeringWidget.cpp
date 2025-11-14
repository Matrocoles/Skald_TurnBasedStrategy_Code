#include "UI/EngineeringWidget.h"
#include "Territory.h"
#include "UI/SkaldMainHUDWidget.h"

void UEngineeringWidget::Setup(ATerritory *InCapital, USkaldMainHUDWidget *InHUD) {
  CapitalTerritory = InCapital;
  OwningHUD = InHUD;
}

void UEngineeringWidget::RequestUpgradeMainGate() {
  if (OwningHUD.IsValid() && CapitalTerritory.IsValid()) {
    OwningHUD->SubmitEngineeringAction(CapitalTerritory->TerritoryID,
                                       EEngineeringAction::UpgradeMainGate);
  }
}

void UEngineeringWidget::RequestBuildMoat() {
  if (OwningHUD.IsValid() && CapitalTerritory.IsValid()) {
    OwningHUD->SubmitEngineeringAction(CapitalTerritory->TerritoryID,
                                       EEngineeringAction::BuildMoat);
  }
}

void UEngineeringWidget::RequestBuildSiegeWeapon() {
  if (OwningHUD.IsValid() && CapitalTerritory.IsValid()) {
    OwningHUD->SubmitEngineeringAction(CapitalTerritory->TerritoryID,
                                       EEngineeringAction::BuildSiegeWeapon);
  }
}

void UEngineeringWidget::RequestCancel() {
  if (OwningHUD.IsValid()) {
    OwningHUD->CancelEngineeringSelection();
  }
}

int32 UEngineeringWidget::GetSiegeGoldCost(ESiegeWeapon SiegeType) const {
  if (OwningHUD.IsValid()) {
    return OwningHUD->GetSiegeGoldCost(SiegeType);
  }

  return SkaldConstants::DefaultSiegeGoldCost;
}
