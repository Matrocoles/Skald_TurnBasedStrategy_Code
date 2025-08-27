#pragma once

#include "Blueprint/UserWidget.h"
#include "DeployWidget.generated.h"

class UButton;
class USpinBox;
class ATerritory;
class ASkaldPlayerState;
class USkaldMainHUDWidget;

/**
 * Simple widget allowing the player to choose how many units to deploy to a
 * territory.
*/
UCLASS(BlueprintType, Blueprintable)
class SKALD_API UDeployWidget : public UUserWidget {
  GENERATED_BODY()

public:
  virtual void NativeConstruct() override;

  /** Configure the widget for the given territory and player state. */
  void Setup(ATerritory *InTerritory, ASkaldPlayerState *InPlayerState,
             USkaldMainHUDWidget *InHUD, int32 MaxAmount);

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  USpinBox *AmountSelector;

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UButton *AcceptButton;

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UButton *DeclineButton;

private:
  UFUNCTION()
  void HandleAccept();

  UFUNCTION()
  void HandleDecline();

  UPROPERTY()
  ATerritory *Territory;

  UPROPERTY()
  ASkaldPlayerState *PlayerState;

  UPROPERTY()
  TWeakObjectPtr<USkaldMainHUDWidget> OwningHUD;
};

