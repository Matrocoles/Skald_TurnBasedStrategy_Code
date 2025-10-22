#include "UI/W_DiceResolutionPanel.h"
#include "Components/BorderSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/SlateWrapperTypes.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Math/UnrealMathUtility.h"
#include "TimerManager.h"
#include "Math/Vector2D.h"
#include "Types/SlateEnums.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Runtime/Launch/Resources/Version.h"

namespace
{
constexpr float FirstRevealDelaySeconds = 0.2f;
constexpr float SubsequentRevealDelaySeconds = 0.25f;
constexpr float CompletionDelaySeconds = 0.2f;
constexpr float DiceOutcomeImageSize = 112.f;

struct FCanvasPanelSlotSnapshot
{
    void Capture(const UCanvasPanelSlot& Slot)
    {
        Anchors = Slot.GetAnchors();
        Offsets = Slot.GetOffsets();
        Alignment = Slot.GetAlignment();
        bAutoSize = Slot.GetAutoSize();
        ZOrder = Slot.GetZOrder();
        bValid = true;
    }

    void Apply(UCanvasPanelSlot& Slot) const
    {
        if (!bValid)
        {
            return;
        }

        Slot.SetAnchors(Anchors);
        Slot.SetOffsets(Offsets);
        Slot.SetAlignment(Alignment);
        Slot.SetAutoSize(bAutoSize);
        Slot.SetZOrder(ZOrder);
    }

    bool bValid = false;
    FAnchors Anchors;
    FMargin Offsets;
    FVector2D Alignment;
    bool bAutoSize = false;
    int32 ZOrder = 0;
};

struct FOverlaySlotSnapshot
{
    void Capture(const UOverlaySlot& Slot)
    {
        Padding = Slot.GetPadding();
        HorizontalAlignment = Slot.GetHorizontalAlignment();
        VerticalAlignment = Slot.GetVerticalAlignment();
        bValid = true;
    }

    void Apply(UOverlaySlot& Slot) const
    {
        if (!bValid)
        {
            return;
        }

        Slot.SetPadding(Padding);
        Slot.SetHorizontalAlignment(HorizontalAlignment);
        Slot.SetVerticalAlignment(VerticalAlignment);
    }

    bool bValid = false;
    FMargin Padding;
    EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
    EVerticalAlignment VerticalAlignment = VAlign_Fill;
};

struct FBorderSlotSnapshot
{
    void Capture(const UBorderSlot& Slot)
    {
        Padding = Slot.GetPadding();
        HorizontalAlignment = Slot.GetHorizontalAlignment();
        VerticalAlignment = Slot.GetVerticalAlignment();
        bValid = true;
    }

    void Apply(UBorderSlot& Slot) const
    {
        if (!bValid)
        {
            return;
        }

        Slot.SetPadding(Padding);
        Slot.SetHorizontalAlignment(HorizontalAlignment);
        Slot.SetVerticalAlignment(VerticalAlignment);
    }

    bool bValid = false;
    FMargin Padding;
    EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
    EVerticalAlignment VerticalAlignment = VAlign_Fill;
};

struct FSizeBoxSlotSnapshot
{
    void Capture(const USizeBoxSlot& Slot)
    {
        Padding = Slot.GetPadding();
        HorizontalAlignment = Slot.GetHorizontalAlignment();
        VerticalAlignment = Slot.GetVerticalAlignment();
        bValid = true;
    }

    void Apply(USizeBoxSlot& Slot) const
    {
        if (!bValid)
        {
            return;
        }

        Slot.SetPadding(Padding);
        Slot.SetHorizontalAlignment(HorizontalAlignment);
        Slot.SetVerticalAlignment(VerticalAlignment);
    }

    bool bValid = false;
    FMargin Padding;
    EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
    EVerticalAlignment VerticalAlignment = VAlign_Fill;
};

struct FVerticalBoxSlotSnapshot
{
    void Capture(const UVerticalBoxSlot& Slot)
    {
        Padding = Slot.GetPadding();
        Size = Slot.GetSize();
        HorizontalAlignment = Slot.GetHorizontalAlignment();
        VerticalAlignment = Slot.GetVerticalAlignment();
        bValid = true;
    }

    void Apply(UVerticalBoxSlot& Slot) const
    {
        if (!bValid)
        {
            return;
        }

        Slot.SetPadding(Padding);
        Slot.SetSize(Size);
        Slot.SetHorizontalAlignment(HorizontalAlignment);
        Slot.SetVerticalAlignment(VerticalAlignment);
    }

    bool bValid = false;
    FMargin Padding;
    FSlateChildSize Size;
    EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
    EVerticalAlignment VerticalAlignment = VAlign_Fill;
};

struct FHorizontalBoxSlotSnapshot
{
    void Capture(const UHorizontalBoxSlot& Slot)
    {
        Padding = Slot.GetPadding();
        Size = Slot.GetSize();
        HorizontalAlignment = Slot.GetHorizontalAlignment();
        VerticalAlignment = Slot.GetVerticalAlignment();
        bValid = true;
    }

    void Apply(UHorizontalBoxSlot& Slot) const
    {
        if (!bValid)
        {
            return;
        }

        Slot.SetPadding(Padding);
        Slot.SetSize(Size);
        Slot.SetHorizontalAlignment(HorizontalAlignment);
        Slot.SetVerticalAlignment(VerticalAlignment);
    }

    bool bValid = false;
    FMargin Padding;
    FSlateChildSize Size;
    EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
    EVerticalAlignment VerticalAlignment = VAlign_Fill;
};

struct FGridSlotSnapshot
{
    void Capture(const UGridSlot& Slot)
    {
        Padding = Slot.GetPadding();
        HorizontalAlignment = Slot.GetHorizontalAlignment();
        VerticalAlignment = Slot.GetVerticalAlignment();
        Row = Slot.GetRow();
        RowSpan = Slot.GetRowSpan();
        Column = Slot.GetColumn();
        ColumnSpan = Slot.GetColumnSpan();
        Layer = Slot.GetLayer();
        Nudge = Slot.GetNudge();
        bValid = true;
    }

    void Apply(UGridSlot& Slot) const
    {
        if (!bValid)
        {
            return;
        }

        Slot.SetPadding(Padding);
        Slot.SetHorizontalAlignment(HorizontalAlignment);
        Slot.SetVerticalAlignment(VerticalAlignment);
        Slot.SetRow(Row);
        Slot.SetRowSpan(RowSpan);
        Slot.SetColumn(Column);
        Slot.SetColumnSpan(ColumnSpan);
        Slot.SetLayer(Layer);
        Slot.SetNudge(Nudge);
    }

    bool bValid = false;
    FMargin Padding;
    EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
    EVerticalAlignment VerticalAlignment = VAlign_Fill;
    int32 Row = 0;
    int32 RowSpan = 1;
    int32 Column = 0;
    int32 ColumnSpan = 1;
    int32 Layer = 0;
    FVector2D Nudge = FVector2D::ZeroVector;
};

FMargin GetUniformGridSlotPadding(const UUniformGridSlot& Slot)
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
    return Slot.GetSlotPadding();
#else
    return Slot.GetPadding();
#endif
}

void SetUniformGridSlotPadding(UUniformGridSlot& Slot, const FMargin& Padding)
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
    Slot.SetSlotPadding(Padding);
#else
    Slot.SetPadding(Padding);
#endif
}

struct FUniformGridSlotSnapshot
{
    void Capture(const UUniformGridSlot& Slot)
    {
        Padding = GetUniformGridSlotPadding(Slot);
        HorizontalAlignment = Slot.GetHorizontalAlignment();
        VerticalAlignment = Slot.GetVerticalAlignment();
        Row = Slot.GetRow();
        Column = Slot.GetColumn();
        bValid = true;
    }

    void Apply(UUniformGridSlot& Slot) const
    {
        if (!bValid)
        {
            return;
        }

        SetUniformGridSlotPadding(Slot, Padding);
        Slot.SetHorizontalAlignment(HorizontalAlignment);
        Slot.SetVerticalAlignment(VerticalAlignment);
        Slot.SetRow(Row);
        Slot.SetColumn(Column);
    }

    bool bValid = false;
    FMargin Padding;
    EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
    EVerticalAlignment VerticalAlignment = VAlign_Fill;
    int32 Row = 0;
    int32 Column = 0;
};
}

UW_DiceResolutionPanel::UW_DiceResolutionPanel(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    OutcomeEntryWidgetClass = UW_DiceResolutionEntryWidget::StaticClass();
}

void UW_DiceResolutionPanel::NativeConstruct()
{
    Super::NativeConstruct();
    InitializeOutcomeScrollContainer();
    ResetPanel();
}

void UW_DiceResolutionPanel::NativeDestruct()
{
    ResetPanel();
    Super::NativeDestruct();
}

void UW_DiceResolutionPanel::BeginResolution(const FDiceRollResult& Result)
{
    ResetPanel();

    ActiveResult = Result;
    RevealIndex = 0;
    RevealedHits = 0;
    RevealedMisses = 0;
    RevealedCrits = 0;
    bResolutionActive = true;

    if (ResolveProgressPlaceholder)
    {
        ResolveProgressPlaceholder->SetVisibility(ESlateVisibility::HitTestInvisible);
    }

    UpdateSummaryLabels();

    const bool bHasDice = ActiveResult.DiceOutcomes.Num() > 0;
    if (!bHasDice)
    {
        RevealedHits = ActiveResult.HitCount;
        RevealedMisses = ActiveResult.MissCount;
        RevealedCrits = ActiveResult.CriticalHitCount;
    }

    UpdateTallies();

    if (!bHasDice)
    {
        ScheduleCompletionDelay(CompletionDelaySeconds);
        return;
    }

    RevealNextDie();
}

void UW_DiceResolutionPanel::ResetPanel()
{
    InitializeOutcomeScrollContainer();

    if (UWorld* World = GetWorld())
    {
        FTimerManager& TimerManager = World->GetTimerManager();
        TimerManager.ClearTimer(RevealTimerHandle);
        TimerManager.ClearTimer(CompletionTimerHandle);
    }

    ClearOutcomeEntries();

    ActiveResult = FDiceRollResult();
    RevealIndex = 0;
    RevealedHits = 0;
    RevealedMisses = 0;
    RevealedCrits = 0;
    bResolutionActive = false;

    if (ResolveProgressPlaceholder)
    {
        ResolveProgressPlaceholder->SetVisibility(ESlateVisibility::Collapsed);
    }

    UpdateSummaryLabels();
    UpdateTallies();
}

void UW_DiceResolutionPanel::SetDiceFaceTextures(const TArray<UTexture2D*>& InTextures)
{
    DiceFaceTextures.Reset(InTextures.Num());
    for (UTexture2D* Texture : InTextures)
    {
        DiceFaceTextures.Add(Texture);
    }
}

void UW_DiceResolutionPanel::ClearOutcomeEntries()
{
    if (OutcomeList)
    {
        OutcomeList->ClearChildren();
    }

    if (OutcomeScrollBox)
    {
        OutcomeScrollBox->ScrollToStart();
    }
}

void UW_DiceResolutionPanel::InitializeOutcomeScrollContainer()
{
    if (!OutcomeList)
    {
        OutcomeScrollBox = nullptr;
        return;
    }

    if (OutcomeScrollBox)
    {
        if (OutcomeList->GetParent() != OutcomeScrollBox)
        {
            OutcomeScrollBox->ClearChildren();
            OutcomeScrollBox->AddChild(OutcomeList);
        }

        ConfigureOutcomeScrollBox(*OutcomeScrollBox);
        return;
    }

    if (UScrollBox* ExistingScroll = Cast<UScrollBox>(OutcomeList->GetParent()))
    {
        OutcomeScrollBox = ExistingScroll;
        ConfigureOutcomeScrollBox(*OutcomeScrollBox);
        return;
    }

    if (!WidgetTree)
    {
        return;
    }

    USizeBox* ScrollSizer = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OutcomeScrollSizer"));
    UScrollBox* NewScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("OutcomeScrollBox"));
    if (!ScrollSizer || !NewScrollBox)
    {
        return;
    }

    ConfigureOutcomeScrollBox(*NewScrollBox);

    const float WindowHeight = GetOutcomeScrollWindowHeight();
    ScrollSizer->SetHeightOverride(WindowHeight);
    ScrollSizer->SetMinDesiredHeight(WindowHeight);
    ScrollSizer->SetMaxDesiredHeight(WindowHeight);

    bool bContainerInserted = false;

    if (WidgetTree->RootWidget == OutcomeList)
    {
        WidgetTree->RootWidget = ScrollSizer;
        bContainerInserted = true;
    }
    else if (UPanelWidget* ParentPanel = OutcomeList->GetParent())
    {
        const int32 ChildIndex = ParentPanel->GetChildIndex(OutcomeList);
        if (ChildIndex != INDEX_NONE)
        {
            FCanvasPanelSlotSnapshot CanvasSnapshot;
            FOverlaySlotSnapshot OverlaySnapshot;
            FBorderSlotSnapshot BorderSnapshot;
            FSizeBoxSlotSnapshot SizeBoxSnapshot;
            FVerticalBoxSlotSnapshot VerticalBoxSnapshot;
            FHorizontalBoxSlotSnapshot HorizontalBoxSnapshot;
            FGridSlotSnapshot GridSnapshot;
            FUniformGridSlotSnapshot UniformGridSnapshot;

            if (const UPanelSlot* ExistingSlot = OutcomeList->Slot)
            {
                if (const UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(ExistingSlot))
                {
                    CanvasSnapshot.Capture(*CanvasSlot);
                }
                if (const UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(ExistingSlot))
                {
                    OverlaySnapshot.Capture(*OverlaySlot);
                }
                if (const UBorderSlot* BorderSlot = Cast<UBorderSlot>(ExistingSlot))
                {
                    BorderSnapshot.Capture(*BorderSlot);
                }
                if (const USizeBoxSlot* SizeBoxSlot = Cast<USizeBoxSlot>(ExistingSlot))
                {
                    SizeBoxSnapshot.Capture(*SizeBoxSlot);
                }
                if (const UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(ExistingSlot))
                {
                    VerticalBoxSnapshot.Capture(*VerticalSlot);
                }
                if (const UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(ExistingSlot))
                {
                    HorizontalBoxSnapshot.Capture(*HorizontalSlot);
                }
                if (const UGridSlot* GridSlot = Cast<UGridSlot>(ExistingSlot))
                {
                    GridSnapshot.Capture(*GridSlot);
                }
                if (const UUniformGridSlot* UniformSlot = Cast<UUniformGridSlot>(ExistingSlot))
                {
                    UniformGridSnapshot.Capture(*UniformSlot);
                }
            }

            OutcomeList->RemoveFromParent();

            if (UPanelSlot* NewSlot = ParentPanel->InsertChildAt(ChildIndex, ScrollSizer))
            {
                if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(NewSlot))
                {
                    CanvasSnapshot.Apply(*CanvasSlot);
                }
                if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(NewSlot))
                {
                    OverlaySnapshot.Apply(*OverlaySlot);
                }
                if (UBorderSlot* BorderSlot = Cast<UBorderSlot>(NewSlot))
                {
                    BorderSnapshot.Apply(*BorderSlot);
                }
                if (USizeBoxSlot* SizeSlot = Cast<USizeBoxSlot>(NewSlot))
                {
                    SizeBoxSnapshot.Apply(*SizeSlot);
                }
                if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(NewSlot))
                {
                    VerticalBoxSnapshot.Apply(*VerticalSlot);
                }
                if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(NewSlot))
                {
                    HorizontalBoxSnapshot.Apply(*HorizontalSlot);
                }
                if (UGridSlot* GridSlot = Cast<UGridSlot>(NewSlot))
                {
                    GridSnapshot.Apply(*GridSlot);
                }
                if (UUniformGridSlot* UniformSlot = Cast<UUniformGridSlot>(NewSlot))
                {
                    UniformGridSnapshot.Apply(*UniformSlot);
                }

                bContainerInserted = true;
            }
            else
            {
                ParentPanel->InsertChildAt(ChildIndex, OutcomeList);
            }
        }
    }

    if (!bContainerInserted)
    {
        return;
    }

    ScrollSizer->SetContent(NewScrollBox);
    OutcomeScrollBox = NewScrollBox;
    OutcomeScrollBox->AddChild(OutcomeList);
}

void UW_DiceResolutionPanel::ConfigureOutcomeScrollBox(UScrollBox& ScrollBox) const
{
    ScrollBox.SetOrientation(EOrientation::Orient_Vertical);
    ScrollBox.SetScrollBarVisibility(ESlateVisibility::Collapsed);
    ScrollBox.SetAllowOverscroll(false);
    ScrollBox.SetAlwaysShowScrollbar(false);
    ScrollBox.SetAnimateWheelScrolling(false);
    ScrollBox.SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
}

float UW_DiceResolutionPanel::GetOutcomeScrollWindowHeight() const
{
    const float EntryPadding = OutcomeEntryPadding.Top + OutcomeEntryPadding.Bottom;
    const float SingleEntryHeight = DiceOutcomeImageSize + EntryPadding;
    return SingleEntryHeight * 2.f;
}

void UW_DiceResolutionPanel::ScheduleNextReveal(float DelaySeconds)
{
    if (!bResolutionActive)
    {
        return;
    }

    if (UWorld* World = GetWorld())
    {
        FTimerManager& TimerManager = World->GetTimerManager();
        TimerManager.ClearTimer(CompletionTimerHandle);
        TimerManager.ClearTimer(RevealTimerHandle);
        TimerManager.SetTimer(RevealTimerHandle, this, &UW_DiceResolutionPanel::RevealNextDie, DelaySeconds, false);
    }
    else
    {
        RevealNextDie();
    }
}

void UW_DiceResolutionPanel::ScheduleCompletionDelay(float DelaySeconds)
{
    if (UWorld* World = GetWorld())
    {
        FTimerManager& TimerManager = World->GetTimerManager();
        TimerManager.ClearTimer(RevealTimerHandle);
        TimerManager.ClearTimer(CompletionTimerHandle);
        TimerManager.SetTimer(CompletionTimerHandle, this, &UW_DiceResolutionPanel::HandleCompletionDelayElapsed, DelaySeconds, false);
    }
    else
    {
        HandleCompletionDelayElapsed();
    }
}

void UW_DiceResolutionPanel::RevealNextDie()
{
    if (!bResolutionActive)
    {
        return;
    }

    if (!ActiveResult.DiceOutcomes.IsValidIndex(RevealIndex))
    {
        HandleCompletionDelayElapsed();
        return;
    }

    const int32 CurrentIndex = RevealIndex;
    const FDiceRollOutcome& Outcome = ActiveResult.DiceOutcomes[CurrentIndex];
    UTexture2D* ResolvedTexture = ResolveDiceTexture(Outcome.RollValue);

    if (OutcomeList)
    {
        bool bEntryAdded = false;
        UWidget* NewlyAddedWidget = nullptr;

        if (OutcomeEntryWidgetClass)
        {
            if (UW_DiceResolutionEntryWidget* EntryWidget = CreateWidget<UW_DiceResolutionEntryWidget>(this, OutcomeEntryWidgetClass))
            {
                EntryWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
                EntryWidget->ConfigureOutcome(Outcome, RevealIndex, ResolvedTexture);

                if (UPanelSlot* AddedSlot = OutcomeList->AddChild(EntryWidget))
                {
                    if (UVerticalBoxSlot* EntrySlot = Cast<UVerticalBoxSlot>(AddedSlot))
                    {
                        EntrySlot->SetPadding(OutcomeEntryPadding);
                    }
                }

                bEntryAdded = true;
                NewlyAddedWidget = EntryWidget;
            }
        }

        if (!bEntryAdded)
        {
            UHorizontalBox* EntryRow = NewObject<UHorizontalBox>(OutcomeList);
            if (EntryRow)
            {
                EntryRow->SetVisibility(ESlateVisibility::HitTestInvisible);

                const FText OutcomeLabel = Outcome.bHit
                    ? (Outcome.bCritical
                        ? NSLOCTEXT("SkaldBattle", "DiceOutcomeCrit", "Crit")
                        : NSLOCTEXT("SkaldBattle", "DiceOutcomeHit", "Hit"))
                    : NSLOCTEXT("SkaldBattle", "DiceOutcomeMiss", "Miss");

                const FLinearColor OutcomeColour = Outcome.bHit
                    ? (Outcome.bCritical ? FLinearColor(0.98f, 0.78f, 0.15f) : FLinearColor(0.12f, 0.76f, 0.45f))
                    : FLinearColor(0.55f, 0.55f, 0.58f);

                if (ResolvedTexture)
                {
                    UImage* DieImage = NewObject<UImage>(EntryRow);
                    if (DieImage)
                    {
                        DieImage->SetBrushFromTexture(ResolvedTexture, true);
                        DieImage->SetDesiredSizeOverride(FVector2D(72.f, 72.f));
                        DieImage->SetVisibility(ESlateVisibility::HitTestInvisible);

                        if (UHorizontalBoxSlot* ImageSlot = EntryRow->AddChildToHorizontalBox(DieImage))
                        {
                            ImageSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
                            ImageSlot->SetVerticalAlignment(VAlign_Center);
                        }
                    }
                }

                UTextBlock* ValueText = NewObject<UTextBlock>(EntryRow);
                if (ValueText)
                {
                    const FText ValueLabel = FText::Format(
                        NSLOCTEXT("SkaldBattle", "DiceOutcomeEntryValue", "#{0} • {1}"),
                        FText::AsNumber(RevealIndex + 1),
                        FText::AsNumber(FMath::Max(Outcome.RollValue, 0)));
                    ValueText->SetText(ValueLabel);
                    ValueText->SetVisibility(ESlateVisibility::HitTestInvisible);

                    if (UHorizontalBoxSlot* ValueSlot = EntryRow->AddChildToHorizontalBox(ValueText))
                    {
                        ValueSlot->SetVerticalAlignment(VAlign_Center);
                    }
                }

                UTextBlock* OutcomeText = NewObject<UTextBlock>(EntryRow);
                if (OutcomeText)
                {
                    const FText LabelText = FText::Format(
                        NSLOCTEXT("SkaldBattle", "DiceOutcomeEntryLabel", "({0})"),
                        OutcomeLabel);
                    OutcomeText->SetText(LabelText);
                    OutcomeText->SetColorAndOpacity(FSlateColor(OutcomeColour));
                    OutcomeText->SetVisibility(ESlateVisibility::HitTestInvisible);

                    if (UHorizontalBoxSlot* LabelSlot = EntryRow->AddChildToHorizontalBox(OutcomeText))
                    {
                        LabelSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
                        LabelSlot->SetVerticalAlignment(VAlign_Center);
                    }
                }

                if (UVerticalBoxSlot* EntrySlot = OutcomeList->AddChildToVerticalBox(EntryRow))
                {
                    EntrySlot->SetPadding(OutcomeEntryPadding);
                }

                NewlyAddedWidget = EntryRow;
            }
        }

        if (OutcomeScrollBox)
        {
            if (NewlyAddedWidget)
            {
                OutcomeScrollBox->ScrollWidgetIntoView(NewlyAddedWidget, false, EDescendantScrollDestination::BottomOrRight);
            }
            else
            {
                OutcomeScrollBox->ScrollToEnd();
            }
        }
    }

    if (Outcome.bHit)
    {
        ++RevealedHits;
        if (Outcome.bCritical)
        {
            ++RevealedCrits;
        }
    }
    else
    {
        ++RevealedMisses;
    }

    UpdateTallies();

    OnDiceOutcomeRevealed.Broadcast(Outcome, CurrentIndex);

    ++RevealIndex;

    if (RevealIndex >= ActiveResult.DiceOutcomes.Num())
    {
        ScheduleCompletionDelay(CompletionDelaySeconds);
    }
    else
    {
        const float NextDelay = (CurrentIndex == 0)
            ? FirstRevealDelaySeconds
            : SubsequentRevealDelaySeconds;
        ScheduleNextReveal(NextDelay);
    }
}

void UW_DiceResolutionPanel::HandleCompletionDelayElapsed()
{
    if (!bResolutionActive)
    {
        BroadcastCompletion();
        ResetPanel();
        return;
    }

    const bool bWasActive = bResolutionActive;
    bResolutionActive = false;

    if (ResolveProgressPlaceholder)
    {
        ResolveProgressPlaceholder->SetVisibility(ESlateVisibility::Collapsed);
    }

    BroadcastCompletion();

    if (bWasActive)
    {
        ResetPanel();
    }
}

void UW_DiceResolutionPanel::UpdateTallies()
{
    const FText HitsText = FText::Format(
        NSLOCTEXT("SkaldBattle", "DiceHitsFormat", "Hits: {0}"),
        FText::AsNumber(RevealedHits));
    if (HitCountText)
    {
        HitCountText->SetText(HitsText);
    }

    const FText MissesText = FText::Format(
        NSLOCTEXT("SkaldBattle", "DiceMissesFormat", "Misses: {0}"),
        FText::AsNumber(RevealedMisses));
    if (MissCountText)
    {
        MissCountText->SetText(MissesText);
    }

    const FText CritsText = FText::Format(
        NSLOCTEXT("SkaldBattle", "DiceCritsFormat", "Crits: {0}"),
        FText::AsNumber(RevealedCrits));
    if (CritCountText)
    {
        CritCountText->SetText(CritsText);
    }
}

void UW_DiceResolutionPanel::UpdateSummaryLabels()
{
    const bool bHasMeaningfulHealth = ActiveResult.StartingHealth > 0 || ActiveResult.EndingHealth > 0;
    const bool bHasDamage = ActiveResult.TotalDamage > 0;

    if (TotalDamageText)
    {
        if (!bHasDamage && !bHasMeaningfulHealth && !bResolutionActive)
        {
            TotalDamageText->SetText(FText::GetEmpty());
        }
        else
        {
            const int32 TotalDamage = FMath::Max(ActiveResult.TotalDamage, 0);
            const FText DamageText = FText::Format(
                NSLOCTEXT("SkaldBattle", "DiceTotalDamageFormat", "Damage: {0}"),
                FText::AsNumber(TotalDamage));
            TotalDamageText->SetText(DamageText);
        }
    }

    if (HealthSummaryText)
    {
        if (!bHasMeaningfulHealth && !bHasDamage && !bResolutionActive)
        {
            HealthSummaryText->SetText(FText::GetEmpty());
        }
        else
        {
            const int32 StartingHealth = FMath::Max(ActiveResult.StartingHealth, 0);
            const int32 EndingHealth = FMath::Max(ActiveResult.EndingHealth, 0);
            const FText HealthText = FText::Format(
                NSLOCTEXT("SkaldBattle", "DiceHealthSummaryFormat", "HP: {0} → {1}"),
                FText::AsNumber(StartingHealth), FText::AsNumber(EndingHealth));
            HealthSummaryText->SetText(HealthText);
        }
    }
}

void UW_DiceResolutionPanel::BroadcastCompletion()
{
    if (UWorld* World = GetWorld())
    {
        FTimerManager& TimerManager = World->GetTimerManager();
        TimerManager.ClearTimer(RevealTimerHandle);
        TimerManager.ClearTimer(CompletionTimerHandle);
    }

    OnResolutionComplete.Broadcast(ActiveResult);
}

UTexture2D* UW_DiceResolutionPanel::ResolveDiceTexture(int32 RollValue) const
{
    if (DiceFaceTextures.Num() == 0)
    {
        return nullptr;
    }

    const int32 Index = RollValue - 1;
    if (DiceFaceTextures.IsValidIndex(Index))
    {
        return DiceFaceTextures[Index];
    }

    return nullptr;
}

UW_DiceResolutionEntryWidget::UW_DiceResolutionEntryWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void UW_DiceResolutionEntryWidget::ConfigureOutcome(const FDiceRollOutcome& Outcome, int32 DisplayIndex, UTexture2D* DieTexture)
{
    const FLinearColor OutcomeColour = Outcome.bHit
        ? (Outcome.bCritical ? CritColour : HitColour)
        : MissColour;

    if (DiceFaceImage)
    {
        if (DieTexture)
        {
            DiceFaceImage->SetBrushFromTexture(DieTexture, true);
            DiceFaceImage->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        else
        {
            DiceFaceImage->SetVisibility(ESlateVisibility::Collapsed);
        }

        DiceFaceImage->SetDesiredSizeOverride(DiceImageSize);
    }

    if (RollValueText)
    {
        const FText ValueLabel = FText::Format(
            NSLOCTEXT("SkaldBattle", "DiceOutcomeEntryValue", "#{0} • {1}"),
            FText::AsNumber(DisplayIndex + 1),
            FText::AsNumber(FMath::Max(Outcome.RollValue, 0)));
        RollValueText->SetText(ValueLabel);
        RollValueText->SetVisibility(ESlateVisibility::HitTestInvisible);
    }

    if (OutcomeLabelText)
    {
        const FText OutcomeLabel = FText::Format(
            NSLOCTEXT("SkaldBattle", "DiceOutcomeEntryLabel", "({0})"),
            Outcome.bHit
                ? (Outcome.bCritical
                    ? NSLOCTEXT("SkaldBattle", "DiceOutcomeCrit", "Crit")
                    : NSLOCTEXT("SkaldBattle", "DiceOutcomeHit", "Hit"))
                : NSLOCTEXT("SkaldBattle", "DiceOutcomeMiss", "Miss"));

        OutcomeLabelText->SetText(OutcomeLabel);
        OutcomeLabelText->SetColorAndOpacity(FSlateColor(OutcomeColour));
        OutcomeLabelText->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
}

