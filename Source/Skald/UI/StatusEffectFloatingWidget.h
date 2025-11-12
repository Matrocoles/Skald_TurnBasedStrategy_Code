#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "StatusEffectFloatingWidget.generated.h"

class SVerticalBox;
class STextBlock;

/**
 * Lightweight widget that renders a vertical list of status effect strings for floating combat indicators.
 */
UCLASS()
class SKALD_API UStatusEffectFloatingWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UStatusEffectFloatingWidget(const FObjectInitializer& ObjectInitializer);

    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

    /** Update the displayed status lines. */
    void SetStatusLines(const TArray<FText>& Lines);

    /** Colour applied to every status entry. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
    FLinearColor TextColor;

    /** Padding applied around each text line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
    FMargin LinePadding;

    /** Font info used when constructing the slate text blocks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
    FSlateFontInfo FontInfo;

protected:
    /** Ensure enough slate text blocks exist to represent the supplied line count. */
    void EnsureLineWidgets(int32 DesiredCount);

    /** Container used to present the list of status entries. */
    TSharedPtr<SVerticalBox> StatusBox;

    /** Cached slate text blocks reused between updates. */
    TArray<TSharedPtr<STextBlock>> CachedLineWidgets;
};

/** Concrete widget used for buff displays. */
UCLASS()
class SKALD_API UBuffFloatingTextWidget : public UStatusEffectFloatingWidget
{
    GENERATED_BODY()

public:
    UBuffFloatingTextWidget(const FObjectInitializer& ObjectInitializer);
};

/** Concrete widget used for debuff displays. */
UCLASS()
class SKALD_API UDebuffFloatingTextWidget : public UStatusEffectFloatingWidget
{
    GENERATED_BODY()

public:
    UDebuffFloatingTextWidget(const FObjectInitializer& ObjectInitializer);
};

