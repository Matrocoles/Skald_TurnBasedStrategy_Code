#include "UI/SkaldTooltipStatics.h"

#include "Blueprint/UserWidget.h"
#include "Internationalization/Text.h"
#include "UI/SkaldTooltipWidget.h"
#include "Components/Widget.h"

void USkaldTooltipStatics::ApplyTooltip(
    UWidget *TargetWidget, TSubclassOf<USkaldTooltipWidget> TooltipClass,
    const FText &TooltipText) {
  if (!TargetWidget) {
    return;
  }

  if (!TooltipClass) {
    TargetWidget->SetToolTipText(TooltipText);
    return;
  }

  if (UWorld *World = TargetWidget->GetWorld()) {
    if (USkaldTooltipWidget *TooltipWidget =
            CreateWidget<USkaldTooltipWidget>(World, TooltipClass)) {
      TooltipWidget->SetTooltipLabelText(TooltipText);
      TargetWidget->SetToolTip(TooltipWidget);
      return;
    }
  }

  TargetWidget->SetToolTipText(TooltipText);
}

void USkaldTooltipStatics::UpgradeExistingTooltip(
    UWidget *TargetWidget, TSubclassOf<USkaldTooltipWidget> TooltipClass) {
  if (!TargetWidget) {
    return;
  }

  const FText ExistingTooltip = TargetWidget->GetToolTipText();
  if (ExistingTooltip.IsEmpty()) {
    return;
  }

  ApplyTooltip(TargetWidget, TooltipClass, ExistingTooltip);
}

FText USkaldTooltipStatics::BuildBasicAbilityTooltip(
    const FSkaldAbilityDefinition &Definition) {
  if (!Definition.IsValid()) {
    return FText::GetEmpty();
  }

  const bool bHasName = !Definition.AbilityName.IsEmpty();
  const bool bHasDescription = !Definition.AbilityDescription.IsEmpty();

  if (bHasName && bHasDescription) {
    return FText::Format(
        NSLOCTEXT("SkaldTooltip", "AbilityTooltipNameDescription", "{0}\n{1}"),
        Definition.AbilityName, Definition.AbilityDescription);
  }

  if (bHasName) {
    return Definition.AbilityName;
  }

  if (bHasDescription) {
    return Definition.AbilityDescription;
  }

  return FText::GetEmpty();
}

