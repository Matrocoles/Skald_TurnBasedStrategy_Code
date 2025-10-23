#include "UI/FighterHealthWidget.h"

#include "SlateOptMacros.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Notifications/SProgressBar.h"

UFighterHealthWidget::UFighterHealthWidget(
    const FObjectInitializer &ObjectInitializer)
    : Super(ObjectInitializer) {
  if (HasAnyFlags(RF_ClassDefaultObject) &&
      GetClass() == UFighterHealthWidget::StaticClass()) {
    BackgroundBrush = *FCoreStyle::Get().GetBrush("WhiteBrush");
    ProgressBarStyle =
        FCoreStyle::Get().GetWidgetStyle<FProgressBarStyle>("ProgressBar");
  }

  DisplayedFraction = 1.f;
  TargetFraction = 1.f;
  TargetFillColor = HealthyFillColor;
  // UUserWidget already supports ticking, so no explicit flag setup is needed.
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
      SNew(SBox)
          .WidthOverride(HealthBarWidth)
          .HeightOverride(HealthBarHeight)
      [SNew(SOverlay)
       + SOverlay::Slot()
             [SNew(SImage)
                  .ColorAndOpacity(BackgroundColor)
                  .Image(&BackgroundBrush)]
       + SOverlay::Slot()[SAssignNew(LocalHealthBar, SProgressBar)
                              .Style(&ProgressBarStyle)
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
  const float ClampedHealthy = FMath::Clamp(HealthyThreshold, 0.f, 1.f);
  const float ClampedWarning =
      FMath::Clamp(WarningThreshold, 0.f, ClampedHealthy);

  if (HealthFraction > ClampedHealthy) {
    return HealthyFillColor;
  }
  if (HealthFraction > ClampedWarning) {
    return WarningFillColor;
  }
  return CriticalFillColor;
}

