#include "UI/FighterSelectionWidget.h"

#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Skald_PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogSkaldUI, Log, All);

bool UFighterSelectionWidget::Initialize()
{
  const bool bInitialized = Super::Initialize();
  if (LockInButton)
  {
    LockInButton->OnClicked.Clear();
    LockInButton->OnClicked.AddDynamic(this, &UFighterSelectionWidget::HandleLockInClicked);
    LockInButton->SetIsEnabled(true);
  }
  else
  {
    UE_LOG(LogSkaldUI, Warning,
           TEXT("FighterSelection: LockInButton not bound (name mismatch?)"));
  }

  return bInitialized;
}

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

  // Disable selection if fighter cannot be afforded.
  if (SelectButton && Owner)
  {
      const bool bCan = Owner->CanAfford(Fighter);
      SelectButton->SetIsEnabled(bCan);
  }
}

void UFighterEntryWidget::HandleClicked() {
  if (Owner) {
    Owner->ChooseFighter(Fighter);
  }
}

void UFighterSelectionWidget::SetAvailableFighters(const TArray<FFighterDefinition>& InFighters)
{
    AvailableFighters = InFighters;
    PopulateFighterList();
}

void UFighterSelectionWidget::SetLockInButtonEnabled(bool bEnabled)
{
  if (LockInButton)
  {
    LockInButton->SetIsEnabled(bEnabled);
  }
}

void UFighterSelectionWidget::NativePreConstruct()
{
    Super::NativePreConstruct();
    UpdateCostDisplay();
#if WITH_EDITOR
    if (IsDesignTime())
    {
        PopulateFighterList();
    }
#endif
}

void UFighterSelectionWidget::NativeConstruct() {
  Super::NativeConstruct();
  SetIsFocusable(true);
  SetFocus();
  SetLockInButtonEnabled(true);
  UpdateCostDisplay();
  PopulateFighterList();
}

void UFighterSelectionWidget::PopulateFighterList() {
  if (!FighterList)
  {
    UE_LOG(LogSkaldUI, Warning, TEXT("[FighterSelection] FighterList is null. "
           "UMG variable must be named 'FighterList' and IsVariable=true."));
    return;
  }
  if (!FighterEntryClass)
  {
    UE_LOG(LogSkaldUI, Warning, TEXT("[FighterSelection] FighterEntryClass is null. "
           "Set it in the widget defaults."));
    return;
  }

  FighterList->ClearChildren();

  if (AvailableFighters.Num() == 0)
  {
    UE_LOG(LogSkaldUI, Warning, TEXT("[FighterSelection] AvailableFighters is EMPTY."));
  }

  int32 Added = 0;
  for (const FFighterDefinition &Fighter : AvailableFighters) {
    if (UFighterEntryWidget *Entry =
            CreateWidget<UFighterEntryWidget>(this, FighterEntryClass)) {
      Entry->Init(Fighter, this);
      FighterList->AddChild(Entry);
      ++Added;
    }
  }

  UE_LOG(LogSkaldUI, Log, TEXT("[FighterSelection] Populated %d fighter entries."), Added);
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
  HandleLockInClicked();
}

void UFighterSelectionWidget::GatherSelectedFighters(
    TArray<FFighterDefinition> &OutFighters) const
{
  OutFighters.Reset();
  OutFighters.Append(ChosenFighters);
}

void UFighterSelectionWidget::HandleLockInClicked()
{
  UE_LOG(LogSkaldUI, Log, TEXT("FighterSelection: Lock In clicked (client)"));

  SetLockInButtonEnabled(false);

  TArray<FFighterDefinition> SelectedFighters;
  GatherSelectedFighters(SelectedFighters);

  bool bSubmitted = false;
  if (APlayerController *PC = GetOwningPlayer())
  {
    if (ASkaldPlayerController *SkaldPC = Cast<ASkaldPlayerController>(PC))
    {
      SkaldPC->Server_LockInSelection(SelectedFighters);
      bSubmitted = true;
    }
  }

  if (!bSubmitted)
  {
    UE_LOG(LogSkaldUI, Warning,
           TEXT("FighterSelection: Unable to submit lock-in (no owning player)"));
    SetLockInButtonEnabled(true);
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
