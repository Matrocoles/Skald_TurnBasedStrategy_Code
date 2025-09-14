#include "UI/FighterSelectionWidget.h"

#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"

void UFighterEntryWidget::NativeConstruct() {
  Super::NativeConstruct();
  if (SelectButton) {
    SelectButton->OnClicked.AddDynamic(this,
                                       &UFighterEntryWidget::HandleClicked);
  }
}

void UFighterEntryWidget::Init(const FFighterDefinition &InFighter,
                               UFighterSelectionWidget *InOwner) {
  Fighter = InFighter;
  Owner = InOwner;
  if (NameText) {
    NameText->SetText(FText::FromName(Fighter.Id));
  }
  if (StrengthText) {
    StrengthText->SetText(FText::AsNumber(Fighter.Stats.Strength));
  }
  if (DefenceText) {
    DefenceText->SetText(FText::AsNumber(Fighter.Stats.Defence));
  }
  if (HealthText) {
    HealthText->SetText(FText::AsNumber(Fighter.Stats.Health));
  }
  if (AttackRangeText) {
    AttackRangeText->SetText(FText::AsNumber(Fighter.Stats.AttackRange));
  }
  if (AttackDamageText) {
    AttackDamageText->SetText(FText::AsNumber(Fighter.Stats.AttackDamage));
  }
  if (AttackDiceText) {
    AttackDiceText->SetText(FText::AsNumber(Fighter.Stats.AttackDice));
  }
  if (MovementText) {
    MovementText->SetText(FText::AsNumber(Fighter.Stats.Movement));
  }
  if (CostText) {
    CostText->SetText(FText::AsNumber(Fighter.Stats.ArmyCost));
  }
}

void UFighterEntryWidget::HandleClicked() {
  if (Owner) {
    Owner->ChooseFighter(Fighter);
  }
}

void UFighterSelectionWidget::NativeConstruct() {
  Super::NativeConstruct();
  if (LockInButton) {
    LockInButton->OnClicked.AddDynamic(this, &UFighterSelectionWidget::LockIn);
  }
  UpdateCostDisplay();
  PopulateFighterList();
}

void UFighterSelectionWidget::PopulateFighterList() {
  if (!FighterList || !FighterEntryClass) {
    return;
  }
  FighterList->ClearChildren();
  for (const FFighterDefinition &Fighter : AvailableFighters) {
    if (UFighterEntryWidget *Entry =
            CreateWidget<UFighterEntryWidget>(this, FighterEntryClass)) {
      Entry->Init(Fighter, this);
      FighterList->AddChild(Entry);
    }
  }
}

bool UFighterSelectionWidget::CanAfford(
    const FFighterDefinition &Fighter) const {
  return CurrentCost + Fighter.Stats.ArmyCost <= MaxCost;
}

bool UFighterSelectionWidget::ChooseFighter(const FFighterDefinition &Fighter) {
  if (!CanAfford(Fighter)) {
    return false;
  }
  ChosenFighters.Add(Fighter);
  CurrentCost += Fighter.Stats.ArmyCost;
  OnFighterChosen.Broadcast(Fighter);
  UpdateCostDisplay();
  return true;
}

void UFighterSelectionWidget::LockIn() {
  if (ChosenFighters.Num() == 0 || CurrentCost > MaxCost) {
    return;
  }
  OnLockedIn.Broadcast();
}

void UFighterSelectionWidget::UpdateCostDisplay() {
  if (CostDisplayText) {
    FFormatNamedArguments Args;
    Args.Add(TEXT("Cur"), CurrentCost);
    Args.Add(TEXT("Max"), MaxCost);
    CostDisplayText->SetText(
        FText::Format(FText::FromString("{Cur} / {Max}"), Args));
  }
}
