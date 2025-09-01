#include "UI/FighterSelectionWidget.h"

#include "Components/Button.h"
#include "Components/ScrollBox.h"

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
}

void UFighterEntryWidget::HandleClicked() {
  if (Owner) {
    Owner->ChooseFighter(Fighter);
  }
}

void UFighterSelectionWidget::NativeConstruct() {
  Super::NativeConstruct();
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
  return true;
}

void UFighterSelectionWidget::LockIn() { OnLockedIn.Broadcast(); }
