#include "UI/LockedInFighterEntryWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "FighterPawn.h"
#include "Internationalization/Text.h"

ULockedInFighterEntryWidget::ULockedInFighterEntryWidget(
    const FObjectInitializer &ObjectInitializer)
    : Super(ObjectInitializer) {}

void ULockedInFighterEntryWidget::NativeConstruct() {
  Super::NativeConstruct();

  if (ClickButton) {
    ClickButton->OnClicked.AddDynamic(this,
                                      &ULockedInFighterEntryWidget::HandleEntryButtonClicked);
  }

  ApplyVisualState();
}

void ULockedInFighterEntryWidget::NativeDestruct() {
  ResetEntry();
  Super::NativeDestruct();
}

void ULockedInFighterEntryWidget::SetFighter(AFighterPawn *InFighter) {
  if (BoundFighter.Get() == InFighter) {
    RefreshFromFighter();
    RefreshTurnState();
    return;
  }

  UnbindFromFighter();
  BindToFighter(InFighter);
}

void ULockedInFighterEntryWidget::RefreshFromFighter() {
  AFighterPawn *Fighter = BoundFighter.Get();
  if (!Fighter) {
    if (PortraitImage) {
      PortraitImage->SetBrushFromTexture(nullptr);
    }
    if (NameText) {
      NameText->SetText(FText::GetEmpty());
    }
    UpdateHealthDisplay(0, 1);
    return;
  }

  if (PortraitImage) {
    PortraitImage->SetBrushFromTexture(Fighter->GetPortraitTexture());
  }

  if (NameText) {
    NameText->SetText(FText::FromString(Fighter->GetHumanReadableName()));
  }

  CachedMaxHealth = FMath::Max(Fighter->GetMaxHealth(), 1);
  UpdateHealthDisplay(Fighter->Stats.Health, CachedMaxHealth);
}

void ULockedInFighterEntryWidget::RefreshTurnState() {
  AFighterPawn *Fighter = BoundFighter.Get();
  if (Fighter) {
    const bool bHasSpentTurn =
        Fighter->HasActivatedThisRound() && Fighter->ActionsRemaining <= 0;
    bIsTurnSpent = bHasSpentTurn;
  } else {
    bIsTurnSpent = false;
  }

  ApplyVisualState();
}

void ULockedInFighterEntryWidget::SetIsSelected(bool bSelected) {
  bIsSelected = bSelected;
  ApplyVisualState();
}

void ULockedInFighterEntryWidget::SetIsActive(bool bActive) {
  bIsActive = bActive;
  ApplyVisualState();
}

void ULockedInFighterEntryWidget::ResetEntry() {
  UnbindFromFighter();
  bIsSelected = false;
  bIsActive = false;
  bIsTurnSpent = false;
  CachedMaxHealth = 1;
  if (PortraitImage) {
    PortraitImage->SetBrushFromTexture(nullptr);
  }
  if (NameText) {
    NameText->SetText(FText::GetEmpty());
  }
  UpdateHealthDisplay(0, 1);
  ApplyVisualState();
}

void ULockedInFighterEntryWidget::HandleEntryButtonClicked() {
  if (AFighterPawn *Fighter = BoundFighter.Get()) {
    OnEntryClicked.Broadcast(Fighter);
  }
}

void ULockedInFighterEntryWidget::HandleFighterHealthChanged(int32 NewHealth) {
  AFighterPawn *Fighter = BoundFighter.Get();
  const int32 MaxHealth = Fighter ? Fighter->GetMaxHealth() : CachedMaxHealth;
  CachedMaxHealth = FMath::Max(MaxHealth, 1);
  UpdateHealthDisplay(NewHealth, CachedMaxHealth);
}

void ULockedInFighterEntryWidget::HandleFighterActionsChanged(
    int32 /*NewActionsRemaining*/) {
  RefreshTurnState();
}

void ULockedInFighterEntryWidget::HandleFighterDestroyed(AActor *DestroyedActor) {
  if (DestroyedActor == BoundFighter.Get()) {
    AFighterPawn *Fighter = BoundFighter.Get();
    OnEntryRemoved.Broadcast(Fighter);
    ResetEntry();
  }
}

void ULockedInFighterEntryWidget::ApplyVisualState() {
  const FLinearColor &DesiredTint = bIsActive
                                        ? ActiveTint
                                        : (bIsSelected ? SelectedTint : NormalTint);
  SetColorAndOpacity(DesiredTint);

  const float DesiredOpacity = bIsTurnSpent ? SpentOpacity : 1.f;
  SetRenderOpacity(DesiredOpacity);

  if (SelectedHighlight) {
    SelectedHighlight->SetVisibility(
        bIsSelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
  }

  if (ActiveHighlight) {
    ActiveHighlight->SetVisibility(
        bIsActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
  }
}

void ULockedInFighterEntryWidget::UpdateHealthDisplay(int32 CurrentHealth,
                                                      int32 MaxHealth) {
  const int32 EffectiveMax = FMath::Max(MaxHealth, 1);
  const float Percent = FMath::Clamp(static_cast<float>(CurrentHealth) /
                                         static_cast<float>(EffectiveMax),
                                     0.f, 1.f);

  if (HealthProgress) {
    HealthProgress->SetPercent(Percent);
  }

  if (HealthText) {
    static const FText HealthFormat = NSLOCTEXT("Skald", "LockedInHealthFormat",
                                                "{0}/{1}");
    HealthText->SetText(
        FText::Format(HealthFormat, FText::AsNumber(CurrentHealth),
                      FText::AsNumber(EffectiveMax)));
  }
}

void ULockedInFighterEntryWidget::BindToFighter(AFighterPawn *Fighter) {
  if (!Fighter) {
    BoundFighter.Reset();
    return;
  }

  BoundFighter = Fighter;
  CachedMaxHealth = FMath::Max(Fighter->GetMaxHealth(), 1);

  Fighter->OnHealthChanged.AddDynamic(this,
                                      &ULockedInFighterEntryWidget::HandleFighterHealthChanged);
  Fighter->OnActionsChanged.AddDynamic(
      this, &ULockedInFighterEntryWidget::HandleFighterActionsChanged);
  Fighter->OnDestroyed.AddDynamic(this,
                                  &ULockedInFighterEntryWidget::HandleFighterDestroyed);

  RefreshFromFighter();
  RefreshTurnState();
}

void ULockedInFighterEntryWidget::UnbindFromFighter() {
  if (AFighterPawn *Fighter = BoundFighter.Get()) {
    Fighter->OnHealthChanged.RemoveDynamic(
        this, &ULockedInFighterEntryWidget::HandleFighterHealthChanged);
    Fighter->OnActionsChanged.RemoveDynamic(
        this, &ULockedInFighterEntryWidget::HandleFighterActionsChanged);
    Fighter->OnDestroyed.RemoveDynamic(
        this, &ULockedInFighterEntryWidget::HandleFighterDestroyed);
  }

  BoundFighter.Reset();
}

