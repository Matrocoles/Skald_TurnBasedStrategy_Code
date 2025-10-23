#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

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

  /** Width of the health bar in Slate units. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Bar",
            meta = (ClampMin = "0.0"))
  float HealthBarWidth;

  /** Height of the health bar in Slate units. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Bar",
            meta = (ClampMin = "0.0"))
  float HealthBarHeight;

  /** Background color applied behind the health bar fill. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Bar")
  FLinearColor BackgroundColor;

  /** Brush used for the background image behind the progress bar. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Bar")
  FSlateBrush BackgroundBrush;

  /** Style applied to the Slate progress bar widget. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Bar")
  FProgressBarStyle ProgressBarStyle;

  /** Lerp speed controlling how quickly the displayed value approaches the target. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Bar",
            meta = (ClampMin = "0.0"))
  float HealthLerpSpeed;

  /** Threshold above which the bar uses the healthy color. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Bar",
            meta = (ClampMin = "0.0", ClampMax = "1.0"))
  float HealthyThreshold;

  /** Threshold above which the bar uses the warning color (and below healthy threshold). */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Bar",
            meta = (ClampMin = "0.0", ClampMax = "1.0"))
  float WarningThreshold;

  /** Color used when health is above the healthy threshold. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Bar")
  FLinearColor HealthyFillColor;

  /** Color used when health is between the warning and critical thresholds. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Bar")
  FLinearColor WarningFillColor;

  /** Color used when health is below the warning threshold. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health Bar")
  FLinearColor CriticalFillColor;

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

