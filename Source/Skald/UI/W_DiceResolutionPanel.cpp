#include "UI/W_DiceResolutionPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/PanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Math/UnrealMathUtility.h"
#include "TimerManager.h"
#include "Math/Vector2D.h"
#include "Skald_GameInstance.h"

namespace
{
constexpr float RevealDelaySeconds = 0.8f;
constexpr float CompletionDelaySeconds = 0.8f;
}

UW_DiceResolutionPanel::UW_DiceResolutionPanel(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    OutcomeEntryWidgetClass = UW_DiceResolutionEntryWidget::StaticClass();
}

void UW_DiceResolutionPanel::NativeConstruct()
{
    Super::NativeConstruct();
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

    UpdateTallies();

    if (ActiveResult.DiceOutcomes.Num() == 0)
    {
        ScheduleCompletionDelay(CompletionDelaySeconds);
        return;
    }

    RevealNextDie();
}

void UW_DiceResolutionPanel::ResetPanel()
{
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

    if (UWorld* World = GetWorld())
    {
        if (USkaldGameInstance* GameInstance = Cast<USkaldGameInstance>(World->GetGameInstance()))
        {
            GameInstance->PlayRandomDiceRollVariant(this);
        }
    }

    const FDiceRollOutcome& Outcome = ActiveResult.DiceOutcomes[RevealIndex];
    UTexture2D* ResolvedTexture = ResolveDiceTexture(Outcome.RollValue);

    if (OutcomeList)
    {
        bool bEntryAdded = false;

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

    ++RevealIndex;

    if (RevealIndex >= ActiveResult.DiceOutcomes.Num())
    {
        ScheduleCompletionDelay(CompletionDelaySeconds);
    }
    else
    {
        ScheduleNextReveal(RevealDelaySeconds);
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

