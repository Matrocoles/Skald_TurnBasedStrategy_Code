#include "UI/SkaldTooltipWidget.h"

#include "Components/Border.h"
#include "Components/TextBlock.h"

USkaldTooltipWidget::USkaldTooltipWidget(
    const FObjectInitializer &ObjectInitializer)
    : Super(ObjectInitializer) {}

void USkaldTooltipWidget::SetTooltipLabelText(const FText &InText) {
  TooltipLabelText = InText;

  if (TooltipLabel) {
    TooltipLabel->SetText(TooltipLabelText);
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
    TooltipLabel->SetText(TooltipLabelText);
  }

  SetRenderTranslation(TooltipOffset);
}

