#pragma once

#include "Blueprint/UserWidget.h"

#include "FighterHealthWidget.generated.h"

/**
 * World-space widget used to display a fighter's current health.
 */
UCLASS()
class SKALD_API UFighterHealthWidget : public UUserWidget {
  GENERATED_BODY()

public:
  UFighterHealthWidget(const FObjectInitializer &ObjectInitializer);

  /** Update the health bar to reflect the provided values. */
  void SetHealthValues(int32 CurrentHealth, int32 MaxHealth);

protected:
  virtual TSharedRef<SWidget> RebuildWidget() override;
  virtual void NativeTick(const FGeometry &MyGeometry,
                          float InDeltaTime) override;

private:
  void UpdateTargetFraction(float NewTargetFraction);
  void ApplyFillColor();
  FLinearColor ResolveFillColor(float HealthFraction) const;

  TSharedPtr<class SProgressBar> HealthBarWidget;

  float DisplayedFraction;
  float TargetFraction;
  FLinearColor TargetFillColor;
};

