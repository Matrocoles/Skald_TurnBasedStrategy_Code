#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/SlateWrapperTypes.h"
#include "GridBattleManager.h"
#include "TimerManager.h"
#include "Templates/SubclassOf.h"
#include "W_DiceResolutionPanel.generated.h"

class UHorizontalBox;
class UImage;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
class UWidget;
class UW_DiceResolutionEntryWidget;

/**
 * Layout overrides that can be applied to the dice resolution panel's canvas slot.
 */
USTRUCT(BlueprintType)
struct SKALD_API FDiceResolutionPanelLayout
{
    GENERATED_BODY();

    /** Whether any overrides should be applied. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout")
    bool bApplyLayout = false;

    /** Apply custom anchors before revealing dice. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout", meta=(EditCondition="bApplyLayout"))
    bool bOverrideAnchors = false;

    /** Anchors to assign when overriding. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout", meta=(EditCondition="bApplyLayout && bOverrideAnchors"))
    FAnchors Anchors = FAnchors(0.5f, 0.5f);

    /** Apply a custom alignment on the slot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout", meta=(EditCondition="bApplyLayout"))
    bool bOverrideAlignment = false;

    /** Alignment to assign when overriding. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout", meta=(EditCondition="bApplyLayout && bOverrideAlignment"))
    FVector2D Alignment = FVector2D(0.5f, 0.5f);

    /** Apply a custom position within the canvas. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout", meta=(EditCondition="bApplyLayout"))
    bool bOverridePosition = false;

    /** Position (in pixels) to use when overriding. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout", meta=(EditCondition="bApplyLayout && bOverridePosition"))
    FVector2D Position = FVector2D::ZeroVector;

    /** Apply a custom size on the canvas slot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout", meta=(EditCondition="bApplyLayout"))
    bool bOverrideSize = false;

    /** Desired size (in pixels) to set when overriding. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout", meta=(EditCondition="bApplyLayout && bOverrideSize"))
    FVector2D Size = FVector2D(512.f, 256.f);
};

/**
 * Displays per-die combat resolution details with staggered reveals.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API UW_DiceResolutionPanel : public UUserWidget
{
    GENERATED_BODY()

public:
    UW_DiceResolutionPanel(const FObjectInitializer& ObjectInitializer);

    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDiceResolutionComplete, const FDiceRollResult&, Result);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDiceOutcomeRevealed, const FDiceRollOutcome&, Outcome, int32, RevealIndex);

    /** Fired after all dice have been revealed and the anticipation pause completes. */
    UPROPERTY(BlueprintAssignable, Category="Skald|Battle|Dice")
    FOnDiceResolutionComplete OnResolutionComplete;

    /** Fired immediately after a single die outcome has been revealed. */
    UPROPERTY(BlueprintAssignable, Category="Skald|Battle|Dice")
    FOnDiceOutcomeRevealed OnDiceOutcomeRevealed;

    /** Begin revealing a new dice resolution result. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle|Dice")
    void BeginResolution(const FDiceRollResult& Result);

    /** Reset the panel to its default empty state. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle|Dice")
    void ResetPanel();

    /** Override the dice face textures used for reveal imagery. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle|Dice")
    void SetDiceFaceTextures(const TArray<UTexture2D*>& InTextures);

protected:
    void ClearOutcomeEntries();
    void ScheduleNextReveal(float DelaySeconds);
    void ScheduleCompletionDelay(float DelaySeconds);
    void RevealNextDie();
    void HandleCompletionDelayElapsed();
    void UpdateTallies();
    void UpdateSummaryLabels();

    /** Text displaying total hits revealed so far. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UTextBlock* HitCountText;

    /** Text displaying total misses revealed so far. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UTextBlock* MissCountText;

    /** Text displaying total critical hits revealed so far. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UTextBlock* CritCountText;

    /** Text displaying the aggregate damage dealt by the attack. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UTextBlock* TotalDamageText;

    /** Text displaying the defender health before and after the attack. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UTextBlock* HealthSummaryText;

    /** Container for per-die outcome entries. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UVerticalBox* OutcomeList;

    /** Placeholder widget for future resolve progress visuals. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UWidget* ResolveProgressPlaceholder;

    /** Dice face textures indexed from 1 to 6 that should be displayed during reveals. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skald|Battle|Dice")
    TArray<TObjectPtr<UTexture2D>> DiceFaceTextures;

    /** Widget class used when creating individual dice outcome entries. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skald|Battle|Dice")
    TSubclassOf<UW_DiceResolutionEntryWidget> OutcomeEntryWidgetClass;

    /** Padding applied to each generated outcome entry slot. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skald|Battle|Dice|Layout")
    FMargin OutcomeEntryPadding = FMargin(0.f, 2.f);

private:
    void BroadcastCompletion();
    UTexture2D* ResolveDiceTexture(int32 RollValue) const;

    /** Copy of the current result used for playback. */
    FDiceRollResult ActiveResult;

    /** Index of the next die outcome to reveal. */
    int32 RevealIndex = 0;

    /** Running tally for hits revealed. */
    int32 RevealedHits = 0;

    /** Running tally for misses revealed. */
    int32 RevealedMisses = 0;

    /** Running tally for critical hits revealed. */
    int32 RevealedCrits = 0;

    /** Whether the panel is currently revealing a result. */
    bool bResolutionActive = false;

    /** Timer driving staggered die reveals. */
    FTimerHandle RevealTimerHandle;

    /** Timer managing the anticipation pause before completion broadcast. */
    FTimerHandle CompletionTimerHandle;
};

/**
 * Entry widget used to present a single dice outcome within the resolution list.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API UW_DiceResolutionEntryWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UW_DiceResolutionEntryWidget(const FObjectInitializer& ObjectInitializer);

    /** Configure the entry visuals based on the revealed outcome. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle|Dice")
    void ConfigureOutcome(const FDiceRollOutcome& Outcome, int32 DisplayIndex, UTexture2D* DieTexture);

protected:
    /** Optional image displaying the dice face. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UImage* DiceFaceImage;

    /** Optional text used to show the dice roll identifier and value. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UTextBlock* RollValueText;

    /** Optional text used to show the hit/crit/miss status. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UTextBlock* OutcomeLabelText;

    /** Colour applied when the outcome is a normal hit. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skald|Battle|Dice|Appearance")
    FLinearColor HitColour = FLinearColor(0.12f, 0.76f, 0.45f);

    /** Colour applied when the outcome is a critical hit. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skald|Battle|Dice|Appearance")
    FLinearColor CritColour = FLinearColor(0.98f, 0.78f, 0.15f);

    /** Colour applied when the outcome is a miss. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skald|Battle|Dice|Appearance")
    FLinearColor MissColour = FLinearColor(0.55f, 0.55f, 0.58f);

    /** Brush size applied to the dice face image when present. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skald|Battle|Dice|Appearance")
    FVector2D DiceImageSize = FVector2D(72.f, 72.f);
};

