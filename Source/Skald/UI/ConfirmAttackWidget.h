#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "ConfirmAttackWidget.generated.h"

class UButton;
class USpinBox;

/**
 * Simple confirmation widget with Approve/Cancel buttons for attack
 * selection.
*/
UCLASS(BlueprintType, Blueprintable)
class SKALD_API UConfirmAttackWidget : public UUserWidget {
  GENERATED_BODY()

public:
  virtual void NativeConstruct() override;

  /** Configure selector for maximum army size available. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Attack")
  void Setup(int32 MaxUnits);

  /** Spin box allowing the player to choose army size. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  USpinBox *ArmySelector;

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UButton *ApproveButton;

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UButton *CancelButton;

  /** Number of units chosen by the player. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Skald|Attack")
  int32 ArmyCount = 1;

private:
  UFUNCTION(BlueprintCallable, Category = "Skald|Attack")
  void HandleValueChanged(float NewValue);
};
