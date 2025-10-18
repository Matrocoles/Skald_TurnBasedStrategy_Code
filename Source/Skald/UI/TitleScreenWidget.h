#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TitleScreenWidget.generated.h"

class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTitleScreenDismissed);

/**
 * Simple full-screen title overlay that fades in a prompt and waits for any input.
 */
UCLASS()
class SKALD_API UTitleScreenWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UTitleScreenWidget(const FObjectInitializer& ObjectInitializer);

    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InTouchEvent) override;

    /** Fired when the user dismisses the title screen. */
    UPROPERTY(BlueprintAssignable, Category="TitleScreen")
    FOnTitleScreenDismissed OnDismissed;

protected:
    void BeginPromptFadeIn();
    void DismissTitle();

    /** Text block showing the "Press any button..." message. */
    UPROPERTY()
    UTextBlock* PressAnyButtonText;

    /** Timer used to delay the prompt's appearance. */
    FTimerHandle PromptDelayTimerHandle;

    /** Current opacity of the prompt text. */
    float PromptOpacity;

    /** Whether the prompt text is currently fading in. */
    bool bFadePromptIn;

    /** Whether the title screen has already been dismissed. */
    bool bHasBeenDismissed;

    /** Duration in seconds for the prompt fade-in animation. */
    static constexpr float PromptFadeDuration = 1.5f;
};
