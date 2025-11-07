#pragma once

#include "Blueprint/UserWidget.h"
#include "DeployWidget.generated.h"

class UButton;
class USpinBox;
class USoundBase;
class ATerritory;
class ASkaldPlayerState;
class USkaldMainHUDWidget;

/**
 * Simple widget allowing the player to choose how many units to deploy or
 * transfer between territories.
 */
UCLASS(BlueprintType, Blueprintable)
class SKALD_API UDeployWidget : public UUserWidget {
  GENERATED_BODY()

public:
  virtual void NativeConstruct() override;

  /** Configure the widget for deploying reinforcements to a territory. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Deploy")
  void SetupDeployment(ATerritory *InTerritory, ASkaldPlayerState *InPlayerState,
                       USkaldMainHUDWidget *InHUD, int32 MaxAmount);

  /** Configure the widget for transferring troops between territories. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Deploy")
  void SetupTransfer(ATerritory *InSource, ATerritory *InTarget,
                     USkaldMainHUDWidget *InHUD, int32 MaxAmount);

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  USpinBox *AmountSelector;

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UButton *AcceptButton;

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UButton *DeclineButton;

  /** Sound to play when the deploy widget is shown. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Deploy")
  TObjectPtr<USoundBase> HowManyTroops;

private:
  UFUNCTION(BlueprintCallable, Category = "Skald|Deploy")
  void HandleAccept();

  UFUNCTION(BlueprintCallable, Category = "Skald|Deploy")
  void HandleDecline();

  enum class EDeployWidgetMode : uint8 { Deployment, Transfer };

  UPROPERTY()
  ATerritory *SourceTerritory = nullptr;

  UPROPERTY()
  ATerritory *TargetTerritory = nullptr;

  UPROPERTY()
  ASkaldPlayerState *PlayerState = nullptr;

  UPROPERTY()
  TWeakObjectPtr<USkaldMainHUDWidget> OwningHUD;

  EDeployWidgetMode Mode = EDeployWidgetMode::Deployment;

  int32 MaxSelectableAmount = 0;
};

