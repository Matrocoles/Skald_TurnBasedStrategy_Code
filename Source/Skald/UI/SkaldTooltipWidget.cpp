#include "UI/SkaldTooltipWidget.h"

#include "Components/Border.h"
#include "Components/TextBlock.h"

USkaldTooltipWidget::USkaldTooltipWidget(
    const FObjectInitializer &ObjectInitializer)
    : Super(ObjectInitializer) {}

void USkaldTooltipWidget::SetTooltipText(const FText &InText) {
  TooltipText = InText;

  if (TooltipLabel) {
    TooltipLabel->SetText(TooltipText);
  }
}

void USkaldTooltipWidget::NativePreConstruct() {
  Super::NativePreConstruct();
  RefreshAppearance();
}

void USkaldTooltipWidget::SynchronizeProperties() {
  Super::SynchronizeProperties();
  RefreshAppearance();
}

void USkaldTooltipWidget::RefreshAppearance() {
  if (TooltipBorder) {
    TooltipBorder->SetBrush(BackgroundBrush);
    TooltipBorder->SetPadding(ContentPadding);
  }

  if (TooltipLabel) {
    TooltipLabel->SetFont(TooltipFont);
    TooltipLabel->SetText(TooltipText);
  }

  SetRenderTranslation(TooltipOffset);
}

