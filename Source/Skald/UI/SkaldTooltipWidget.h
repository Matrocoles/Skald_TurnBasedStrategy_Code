#pragma once

#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "SkaldTooltipWidget.generated.h"

class UBorder;
class UTextBlock;

/**
 * Universal tooltip widget that supports editor-driven styling and cursor offset.
 */
UCLASS(BlueprintType, Blueprintable)
class SKALD_API USkaldTooltipWidget : public UUserWidget {
  GENERATED_BODY()

public:
  USkaldTooltipWidget(const FObjectInitializer &ObjectInitializer);

  /** Text displayed inside the tooltip. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Tooltip")
  void SetTooltipText(const FText &InText);

  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Skald|Tooltip")
  const FText &GetTooltipText() const { return TooltipText; }

  /** Translation applied so the tooltip spawns away from the cursor. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Tooltip")
  FVector2D TooltipOffset = FVector2D(80.f, 50.f);

  /** Font used by the optional text block binding. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Tooltip")
  FSlateFontInfo TooltipFont;

  /** Padding applied to the optional border binding. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Tooltip")
  FMargin ContentPadding = FMargin(12.f, 8.f);

  /** Border brush assigned to the optional border binding. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Tooltip")
  FSlateBrush BackgroundBrush;

protected:
  virtual void NativePreConstruct() override;
  virtual void SynchronizeProperties() override;

  /** Optional border used for styling. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UBorder *TooltipBorder;

  /** Optional text block receiving the tooltip label. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *TooltipLabel;

private:
  void RefreshAppearance();

  /** Cached tooltip text so it can be re-applied when the widget rebuilds. */
  UPROPERTY(Transient)
  FText TooltipText;
};

