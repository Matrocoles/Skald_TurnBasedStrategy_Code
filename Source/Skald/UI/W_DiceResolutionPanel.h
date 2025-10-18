#pragma once

#include "Blueprint/UserWidget.h"
#include "GridBattleManager.h"
#include "TimerManager.h"
#include "W_DiceResolutionPanel.generated.h"

class UTextBlock;
class UVerticalBox;
class UWidget;

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

    /** Fired after all dice have been revealed and the anticipation pause completes. */
    UPROPERTY(BlueprintAssignable, Category="Skald|Battle|Dice")
    FOnDiceResolutionComplete OnResolutionComplete;

    /** Begin revealing a new dice resolution result. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle|Dice")
    void BeginResolution(const FDiceRollResult& Result);

    /** Reset the panel to its default empty state. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle|Dice")
    void ResetPanel();

protected:
    void ClearOutcomeEntries();
    void ScheduleNextReveal(float MinDelay, float MaxDelay);
    void RevealNextDie();
    void HandleCompletionDelayElapsed();
    void UpdateTallies();

    /** Text displaying total hits revealed so far. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UTextBlock* HitCountText;

    /** Text displaying total misses revealed so far. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UTextBlock* MissCountText;

    /** Text displaying total critical hits revealed so far. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UTextBlock* CritCountText;

    /** Container for per-die outcome entries. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UVerticalBox* OutcomeList;

    /** Placeholder widget for future resolve progress visuals. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UWidget* ResolveProgressPlaceholder;

private:
    void BroadcastCompletion();

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

