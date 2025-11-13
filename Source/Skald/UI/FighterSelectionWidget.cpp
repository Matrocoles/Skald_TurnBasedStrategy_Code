#include "UI/FighterSelectionWidget.h"

#include "Abilities/SkaldAbilityTypes.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Internationalization/Text.h"
#include "SkaldLogging.h"
#include "Skald_PlayerController.h"
#include "UI/SkaldTooltipStatics.h"

bool UFighterSelectionWidget::Initialize()
{
  const bool bSuperInitialized = Super::Initialize();
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

  return bSuperInitialized;
}

void UFighterEntryWidget::NativeConstruct() {
  Super::NativeConstruct();
  if (SelectButton) {
    SelectButton->OnClicked.AddDynamic(this,
                                       &UFighterEntryWidget::HandleClicked);
  }
  if (RemoveButton) {
    RemoveButton->OnClicked.AddDynamic(this,
                                       &UFighterEntryWidget::HandleRemoveClicked);
  }

  UpgradeTooltipToUniversal(HealthImage);
  UpgradeTooltipToUniversal(StrengthImage);
  UpgradeTooltipToUniversal(DefenceImage);
  UpgradeTooltipToUniversal(AttackDiceImage);
  UpgradeTooltipToUniversal(AttackDamageImage);
  UpgradeTooltipToUniversal(AttackRangeImage);
  UpgradeTooltipToUniversal(MovementImage);
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
  if (CriticalDamageText) {
    const int32 CriticalDamage =
        Fighter.Stats.AttackDamage + Fighter.Stats.CriticalBonusDamage;
    CriticalDamageText->SetText(FText::AsNumber(CriticalDamage));
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

  PassiveAbilityDefinition = GetFactionPassive(Fighter.Faction);
  ActiveAbilityDefinition =
      GetFactionActiveAbility(Fighter.Faction, Fighter.Stats.ArmyCost);

  if (PassiveAbilityText) {
    if (PassiveAbilityDefinition.IsValid()) {
      PassiveAbilityText->SetText(FText::Format(
          NSLOCTEXT("SkaldAbilities", "PassiveEntryFormat", "{0}\n{1}"),
          PassiveAbilityDefinition.AbilityName,
          PassiveAbilityDefinition.AbilityDescription));
    } else {
      PassiveAbilityText->SetText(
          NSLOCTEXT("SkaldAbilities", "PassiveEntryNone",
                     "No faction passive available."));
    }

  }

  if (ActiveAbilityText) {
    if (ActiveAbilityDefinition.IsValid()) {
      const FText CostLabel = ActiveAbilityDefinition.BuildCostLabel();
      FText CooldownLabel;
      if (ActiveAbilityDefinition.bOncePerBattle) {
        CooldownLabel = NSLOCTEXT("SkaldAbilities", "AbilityOncePerBattleLabel",
                                  "Use: once per battle");
      } else if (ActiveAbilityDefinition.CooldownRounds > 0) {
        CooldownLabel = FText::Format(
            NSLOCTEXT("SkaldAbilities", "AbilityCooldownFormat",
                      "Cooldown: {0} rounds"),
            FText::AsNumber(ActiveAbilityDefinition.CooldownRounds));
      } else {
        CooldownLabel =
            NSLOCTEXT("SkaldAbilities", "AbilityNoCooldownLabel",
                       "Cooldown: none");
      }

      const FText ActiveDisplayText = FText::Format(
          NSLOCTEXT("SkaldAbilities", "ActiveEntryFormat",
                     "{0} ({1})\n{2}\n{3}"),
          ActiveAbilityDefinition.AbilityName, CostLabel,
          ActiveAbilityDefinition.AbilityDescription, CooldownLabel);
      ActiveAbilityText->SetText(ActiveDisplayText);
    } else {
      const FText NoActiveAbilityText = NSLOCTEXT(
          "SkaldAbilities", "ActiveEntryNone", "No active ability assigned.");
      ActiveAbilityText->SetText(NoActiveAbilityText);
    }
  }

  if (PortraitImage)
  {
      UTexture2D* PortraitTexture = nullptr;
      if (Fighter.Portrait.IsValid())
      {
          PortraitTexture = Fighter.Portrait.Get();
      }
      else if (!Fighter.Portrait.IsNull())
      {
          PortraitTexture = Fighter.Portrait.LoadSynchronous();
      }

      if (PortraitTexture)
      {
          PortraitImage->SetBrushFromTexture(PortraitTexture);
      }
      else
      {
          PortraitImage->SetBrushFromTexture(nullptr);
      }
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

void UFighterEntryWidget::HandleRemoveClicked() {
  if (Owner) {
    Owner->RemoveFighter(Fighter);
  }
}

void UFighterEntryWidget::ApplyUniversalTooltip(UWidget *Widget,
                                                const FText &TooltipText) const {
  USkaldTooltipStatics::ApplyTooltip(Widget, UniversalTooltipClass, TooltipText);
}

void UFighterEntryWidget::UpgradeTooltipToUniversal(UWidget *Widget) const {
  if (!Widget) {
    return;
  }

  USkaldTooltipStatics::UpgradeExistingTooltip(Widget, UniversalTooltipClass);
}

UTexture2D* UFighterEntryWidget::GetPortraitTexture() const
{
    if (Fighter.Portrait.IsValid())
    {
        return Fighter.Portrait.Get();
    }

    return nullptr;
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

  AvailableFighters.Sort([](const FFighterDefinition& A, const FFighterDefinition& B)
  {
    return A.Stats.ArmyCost < B.Stats.ArmyCost;
  });

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
  RefreshEntryAffordability();
  return true;
}

bool UFighterSelectionWidget::RemoveFighter(const FFighterDefinition &Fighter)
{
  const int32 Index = ChosenFighters.IndexOfByPredicate(
      [&Fighter](const FFighterDefinition& Candidate)
      {
        return Candidate.Id == Fighter.Id;
      });

  if (Index == INDEX_NONE)
  {
    return false;
  }

  CurrentCost -= ChosenFighters[Index].Stats.ArmyCost;
  ChosenFighters.RemoveAt(Index);
  UpdateCostDisplay();
  RefreshEntryAffordability();
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

void UFighterSelectionWidget::RefreshEntryAffordability()
{
  if (!FighterList)
  {
    return;
  }

  const int32 NumChildren = FighterList->GetChildrenCount();
  for (int32 ChildIdx = 0; ChildIdx < NumChildren; ++ChildIdx)
  {
    if (UFighterEntryWidget* Entry = Cast<UFighterEntryWidget>(FighterList->GetChildAt(ChildIdx)))
    {
      if (Entry->SelectButton)
      {
        Entry->SelectButton->SetIsEnabled(CanAfford(Entry->Fighter));
      }
    }
  }
}
