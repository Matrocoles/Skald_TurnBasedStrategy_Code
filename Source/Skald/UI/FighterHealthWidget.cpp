#include "UI/FighterHealthWidget.h"

#include "SlateOptMacros.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Notifications/SProgressBar.h"

namespace {
constexpr float HealthBarWidth = 120.f;
constexpr float HealthBarHeight = 14.f;
// Lerp speed chosen to reach the target in roughly 0.2 seconds.
constexpr float HealthLerpSpeed = 12.f;
}

UFighterHealthWidget::UFighterHealthWidget(
    const FObjectInitializer &ObjectInitializer)
    : Super(ObjectInitializer), DisplayedFraction(1.f), TargetFraction(1.f),
      TargetFillColor(FLinearColor::Green) {
  bCanEverTick = true;
}

void UFighterHealthWidget::SetHealthValues(int32 CurrentHealth,
                                           int32 MaxHealth) {
  const float SafeMax = MaxHealth > 0 ? static_cast<float>(MaxHealth) : 1.f;
  const float Fraction =
      FMath::Clamp(static_cast<float>(CurrentHealth) / SafeMax, 0.f, 1.f);
  UpdateTargetFraction(Fraction);
}

TSharedRef<SWidget> UFighterHealthWidget::RebuildWidget() {
  DisplayedFraction = TargetFraction;
  TargetFillColor = ResolveFillColor(TargetFraction);

  TSharedPtr<SProgressBar> LocalHealthBar;

  const TSharedRef<SWidget> RootWidget =
      SNew(SBox).WidthOverride(HealthBarWidth).HeightOverride(HealthBarHeight)
      [SNew(SOverlay)
       + SOverlay::Slot()
             [SNew(SImage)
                  .ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.65f))
                  .Image(FCoreStyle::Get().GetBrush("WhiteBrush"))]
       + SOverlay::Slot()[SAssignNew(LocalHealthBar, SProgressBar)
                              .Percent(DisplayedFraction)
                              .FillColorAndOpacity(TargetFillColor)
                              .BarFillType(EProgressBarFillType::LeftToRight)
                              .BorderPadding(FVector2D::ZeroVector)]];

  HealthBarWidget = LocalHealthBar;
  ApplyFillColor();

  return RootWidget;
}

void UFighterHealthWidget::NativeTick(const FGeometry &MyGeometry,
                                      float InDeltaTime) {
  Super::NativeTick(MyGeometry, InDeltaTime);

  if (!HealthBarWidget.IsValid()) {
    return;
  }

  DisplayedFraction = FMath::FInterpTo(DisplayedFraction, TargetFraction,
                                       InDeltaTime, HealthLerpSpeed);
  if (FMath::IsNearlyEqual(DisplayedFraction, TargetFraction, 0.001f)) {
    DisplayedFraction = TargetFraction;
  }

  HealthBarWidget->SetPercent(DisplayedFraction);
  ApplyFillColor();
}

void UFighterHealthWidget::UpdateTargetFraction(float NewTargetFraction) {
  TargetFraction = NewTargetFraction;
  TargetFillColor = ResolveFillColor(TargetFraction);
  ApplyFillColor();
}

void UFighterHealthWidget::ApplyFillColor() {
  if (HealthBarWidget.IsValid()) {
    HealthBarWidget->SetFillColorAndOpacity(TargetFillColor);
  }
}

FLinearColor
UFighterHealthWidget::ResolveFillColor(float HealthFraction) const {
  if (HealthFraction > 0.6f) {
    return FLinearColor(0.133f, 0.698f, 0.298f, 1.f); // Green
  }
  if (HealthFraction > 0.2f) {
    return FLinearColor(0.949f, 0.765f, 0.058f, 1.f); // Yellow
  }
  return FLinearColor(0.835f, 0.066f, 0.066f, 1.f);   // Red
}

