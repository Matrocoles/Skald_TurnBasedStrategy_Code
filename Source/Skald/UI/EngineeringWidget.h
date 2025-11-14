#pragma once

#include "Blueprint/UserWidget.h"
#include "SkaldTypes.h"
#include "EngineeringWidget.generated.h"

class ATerritory;
class USkaldMainHUDWidget;

/**
 * Lightweight bridge widget used to trigger engineering actions from Blueprint UI.
 */
UCLASS(Blueprintable)
class SKALD_API UEngineeringWidget : public UUserWidget {
  GENERATED_BODY()

public:
  UFUNCTION(BlueprintCallable, Category = "Skald|Engineering")
  void Setup(ATerritory *InCapital, USkaldMainHUDWidget *InHUD);

  UFUNCTION(BlueprintCallable, Category = "Skald|Engineering")
  void RequestUpgradeMainGate();

  UFUNCTION(BlueprintCallable, Category = "Skald|Engineering")
  void RequestBuildMoat();

  UFUNCTION(BlueprintCallable, Category = "Skald|Engineering")
  void RequestBuildSiegeWeapon();

  UFUNCTION(BlueprintCallable, Category = "Skald|Engineering")
  void RequestCancel();

  UFUNCTION(BlueprintCallable, Category = "Skald|Engineering")
  int32 GetSiegeGoldCost(ESiegeWeapon SiegeType) const;

private:
  UPROPERTY()
  TWeakObjectPtr<ATerritory> CapitalTerritory;

  UPROPERTY()
  TWeakObjectPtr<USkaldMainHUDWidget> OwningHUD;
};
