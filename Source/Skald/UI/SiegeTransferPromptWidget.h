#pragma once

#include "Blueprint/UserWidget.h"
#include "SiegeTransferPromptWidget.generated.h"

class USkaldMainHUDWidget;

/**
 * Modal widget prompting players to decide whether to move a siege weapon.
 */
UCLASS(Blueprintable)
class SKALD_API USiegeTransferPromptWidget : public UUserWidget {
  GENERATED_BODY()

public:
  UFUNCTION(BlueprintCallable, Category = "Skald|Siege")
  void Setup(int32 FromTerritoryID, int32 ToTerritoryID,
             USkaldMainHUDWidget *InHUD);

  UFUNCTION(BlueprintCallable, Category = "Skald|Siege")
  void ConfirmTransfer();

  UFUNCTION(BlueprintCallable, Category = "Skald|Siege")
  void DeclineTransfer();

private:
  UPROPERTY()
  int32 PendingFromID = -1;

  UPROPERTY()
  int32 PendingToID = -1;

  UPROPERTY()
  TWeakObjectPtr<USkaldMainHUDWidget> OwningHUD;
};
