#pragma once

#include "Abilities/SkaldAbilityTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SkaldTooltipStatics.generated.h"

class USkaldTooltipWidget;
class UWidget;

/** Blueprint helpers shared by widgets that need consistent tooltip behaviour. */
UCLASS()
class SKALD_API USkaldTooltipStatics : public UBlueprintFunctionLibrary {
  GENERATED_BODY()

public:
  /** Apply a tooltip widget (or fallback text) to the supplied widget. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Tooltip",
            meta = (DefaultToSelf = "TargetWidget"))
  static void ApplyTooltip(UWidget *TargetWidget,
                           TSubclassOf<USkaldTooltipWidget> TooltipClass,
                           const FText &TooltipText);

  /** Rebuild the tooltip using the widget's existing tooltip text. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Tooltip",
            meta = (DefaultToSelf = "TargetWidget"))
  static void UpgradeExistingTooltip(
      UWidget *TargetWidget, TSubclassOf<USkaldTooltipWidget> TooltipClass);

  /** Helper used by multiple widgets to build name/description tooltips. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Tooltip")
  static FText BuildBasicAbilityTooltip(const FSkaldAbilityDefinition &Definition);
};

