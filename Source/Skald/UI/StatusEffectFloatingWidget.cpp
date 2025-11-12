#include "UI/StatusEffectFloatingWidget.h"

#include "SlateOptMacros.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"

UStatusEffectFloatingWidget::UStatusEffectFloatingWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    TextColor = FLinearColor::White;
    LinePadding = FMargin(0.f, 0.f, 0.f, 2.f);
    FontInfo = FCoreStyle::GetDefaultFontStyle("Regular", 18);
}

TSharedRef<SWidget> UStatusEffectFloatingWidget::RebuildWidget()
{
    CachedLineWidgets.Reset();

    SAssignNew(StatusBox, SVerticalBox);

    return StatusBox.ToSharedRef();
}

void UStatusEffectFloatingWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    StatusBox.Reset();
    CachedLineWidgets.Reset();
}

void UStatusEffectFloatingWidget::EnsureLineWidgets(int32 DesiredCount)
{
    if (!StatusBox.IsValid())
    {
        return;
    }

    while (CachedLineWidgets.Num() < DesiredCount)
    {
        TSharedPtr<STextBlock> NewTextBlock;
        StatusBox->AddSlot()
            .AutoHeight()
            .Padding(LinePadding)
            [
                SAssignNew(NewTextBlock, STextBlock)
                .Text(FText::GetEmpty())
                .Justification(ETextJustify::Center)
                .ColorAndOpacity(TextColor)
                .Font(FontInfo)
            ];

        CachedLineWidgets.Add(NewTextBlock);
    }
}

void UStatusEffectFloatingWidget::SetStatusLines(const TArray<FText>& Lines)
{
    if (!StatusBox.IsValid())
    {
        return;
    }

    EnsureLineWidgets(Lines.Num());

    for (int32 Index = 0; Index < CachedLineWidgets.Num(); ++Index)
    {
        if (!CachedLineWidgets[Index].IsValid())
        {
            continue;
        }

        if (Index < Lines.Num())
        {
            CachedLineWidgets[Index]->SetText(Lines[Index]);
            CachedLineWidgets[Index]->SetColorAndOpacity(TextColor);
            CachedLineWidgets[Index]->SetVisibility(EVisibility::SelfHitTestInvisible);
        }
        else
        {
            CachedLineWidgets[Index]->SetText(FText::GetEmpty());
            CachedLineWidgets[Index]->SetVisibility(EVisibility::Collapsed);
        }
    }

    if (StatusBox.IsValid())
    {
        StatusBox->Invalidate(EInvalidateWidget::LayoutAndVolatility);
    }
}

UBuffFloatingTextWidget::UBuffFloatingTextWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    TextColor = FLinearColor(0.25f, 0.95f, 0.45f, 1.f);
}

UDebuffFloatingTextWidget::UDebuffFloatingTextWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    TextColor = FLinearColor(0.95f, 0.35f, 0.35f, 1.f);
}

