#include "UI/TitleScreenWidget.h"

#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Blueprint/WidgetTree.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"

namespace
{
    constexpr float PromptDelaySeconds = 3.0f;
}

UTitleScreenWidget::UTitleScreenWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , PressAnyButtonText(nullptr)
    , PromptOpacity(0.0f)
    , bFadePromptIn(false)
    , bHasBeenDismissed(false)
{
    bIsFocusable = true;
}

void UTitleScreenWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (WidgetTree && !WidgetTree->RootWidget)
    {
        UOverlay* RootOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
        WidgetTree->RootWidget = RootOverlay;

        UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
        Background->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.9f));
        Background->SetPadding(FMargin(0.f));
        if (UOverlaySlot* BackgroundSlot = RootOverlay->AddChildToOverlay(Background))
        {
            BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
            BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
        }

        UVerticalBox* ContentBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
        if (UOverlaySlot* ContentSlot = RootOverlay->AddChildToOverlay(ContentBox))
        {
            ContentSlot->SetHorizontalAlignment(HAlign_Center);
            ContentSlot->SetVerticalAlignment(VAlign_Center);
        }

        UTextBlock* TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        TitleText->SetText(FText::FromString(TEXT("SKALD")));
        TitleText->SetJustification(ETextJustify::Center);
        TitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 72);
        TitleText->SetFont(TitleFont);
        if (UVerticalBoxSlot* TitleSlot = ContentBox->AddChildToVerticalBox(TitleText))
        {
            TitleSlot->SetHorizontalAlignment(HAlign_Center);
            TitleSlot->SetVerticalAlignment(VAlign_Center);
            TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 32.f));
        }

        PressAnyButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        PressAnyButtonText->SetText(FText::FromString(TEXT("Press any button...")));
        PressAnyButtonText->SetJustification(ETextJustify::Center);
        PressAnyButtonText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        FSlateFontInfo PromptFont = FCoreStyle::GetDefaultFontStyle("Regular", 32);
        PressAnyButtonText->SetFont(PromptFont);
        PressAnyButtonText->SetRenderOpacity(0.0f);
        PressAnyButtonText->SetVisibility(ESlateVisibility::HitTestInvisible);
        if (UVerticalBoxSlot* PromptSlot = ContentBox->AddChildToVerticalBox(PressAnyButtonText))
        {
            PromptSlot->SetHorizontalAlignment(HAlign_Center);
            PromptSlot->SetVerticalAlignment(VAlign_Center);
        }
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(PromptDelayTimerHandle, this, &UTitleScreenWidget::BeginPromptFadeIn, PromptDelaySeconds, false);
    }
}

void UTitleScreenWidget::NativeDestruct()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(PromptDelayTimerHandle);
    }

    Super::NativeDestruct();
}

void UTitleScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (bFadePromptIn && PressAnyButtonText)
    {
        PromptOpacity = FMath::Clamp(PromptOpacity + (InDeltaTime / PromptFadeDuration), 0.0f, 1.0f);
        PressAnyButtonText->SetRenderOpacity(PromptOpacity);
        if (PromptOpacity >= 1.0f)
        {
            bFadePromptIn = false;
        }
    }
}

FReply UTitleScreenWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    DismissTitle();
    return FReply::Handled();
}

FReply UTitleScreenWidget::NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent)
{
    DismissTitle();
    return FReply::Handled();
}

FReply UTitleScreenWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    DismissTitle();
    return FReply::Handled();
}

FReply UTitleScreenWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InTouchEvent)
{
    DismissTitle();
    return FReply::Handled();
}

void UTitleScreenWidget::BeginPromptFadeIn()
{
    bFadePromptIn = true;
}

void UTitleScreenWidget::DismissTitle()
{
    if (bHasBeenDismissed)
    {
        return;
    }

    bHasBeenDismissed = true;
    OnDismissed.Broadcast();
    RemoveFromParent();
}
