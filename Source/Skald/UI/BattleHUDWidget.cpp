#include "UI/BattleHUDWidget.h"
#include "SkaldLogging.h"
#include "Skald_PlayerController.h"
#include "Components/Button.h"
#include "Components/CapsuleComponent.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Engine/World.h"
#include "FighterPawn.h"
#include "GridOverlayComponent.h"
#include "UI/CombatFloaterPoolSubsystem.h"
#include "UI/LockedInFighterEntryWidget.h"
#include "UI/SkaldTooltipStatics.h"
#include "UI/W_DiceResolutionPanel.h"
#include "UI/W_FloatingText.h"
#include "Math/UnrealMathUtility.h"
#include "Math/Vector2D.h"
#include "TimerManager.h"
#include "CanvasItem.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/WeakObjectPtrTemplates.h"

namespace {
void AccumulateStatDelta(FSkaldAbilityStatDelta &Target,
                         const FSkaldAbilityStatDelta &Source) {
  Target.AttackDice += Source.AttackDice;
  Target.AttackDamage += Source.AttackDamage;
  Target.MeleeAttackDamage += Source.MeleeAttackDamage;
  Target.RangedAttackDamage += Source.RangedAttackDamage;
  Target.AttackRange += Source.AttackRange;
  Target.Movement += Source.Movement;
  Target.Defence += Source.Defence;
  Target.Strength += Source.Strength;
  Target.CriticalBonusDamage += Source.CriticalBonusDamage;
}

FSkaldAbilityStatDelta GatherNetStatDelta(const AFighterPawn *Fighter) {
  FSkaldAbilityStatDelta TotalDelta;
  if (!Fighter) {
    return TotalDelta;
  }

  for (const FActiveBuff &Buff : Fighter->ActiveBuffs) {
    AccumulateStatDelta(TotalDelta, Buff.Delta);
  }

  for (const FActiveDebuff &Debuff : Fighter->ActiveDebuffs) {
    AccumulateStatDelta(TotalDelta, Debuff.Delta);
  }

  return TotalDelta;
}

int32 ResolveAttackDamageDelta(const AFighterPawn *Fighter,
                               const FSkaldAbilityStatDelta &Delta) {
  int32 Result = Delta.AttackDamage;
  if (!Fighter) {
    return Result;
  }

  switch (Fighter->GetAttackType()) {
  case EFighterAttackType::Melee:
    Result += Delta.MeleeAttackDamage;
    break;
  case EFighterAttackType::Ranged:
    Result += Delta.RangedAttackDamage;
    break;
  default:
    break;
  }

  return Result;
}
} // namespace

UBattleHUDWidget::UBattleHUDWidget(const FObjectInitializer &ObjectInitializer)
    : Super(ObjectInitializer) {
  FloaterWidgetClass = UW_FloatingText::StaticClass();
  CriticalFloaterStyle.Scale = 1.35f;
  HighStakesCriticalFloaterStyle.Color = FLinearColor(0.94f, 0.2f, 0.2f);
  HighStakesCriticalFloaterStyle.Scale = 1.55f;
}

void UBattleHUDWidget::NativeConstruct() {
  Super::NativeConstruct();

  if (MoveButton) {
    MoveButton->OnClicked.AddDynamic(this,
                                     &UBattleHUDWidget::HandleMovePressed);
  }
  if (DisengageButton) {
    DisengageButton->OnClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleDisengagePressed);
    DisengageButton->SetVisibility(ESlateVisibility::Collapsed);
    DisengageButton->SetIsEnabled(true);
  }
  if (AttackButton) {
    AttackButton->OnClicked.AddDynamic(this,
                                       &UBattleHUDWidget::HandleAttackPressed);
  }
  SetActionButtonsVisibility(false);
  if (ActivateButton) {
    ActivateButton->OnClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleActivatePressed);
    ActivateButton->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (EndTurnButton) {
    EndTurnButton->OnClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleEndTurnPressed);
    RefreshEndTurnButtonVisibility();
  }

  if (RollInitiativeButton) {
    RollInitiativeButton->OnClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleInitiativeRollPressed);
    RollInitiativeButton->SetVisibility(ESlateVisibility::Collapsed);
    RollInitiativeButton->SetIsEnabled(true);
  }

  if (AttackRollButton) {
    AttackRollButton->OnClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleAttackRollPressed);
    AttackRollButton->SetVisibility(ESlateVisibility::Collapsed);
    AttackRollButton->SetIsEnabled(true);
  }

  if (AbilityButton1) {
    AbilityButton1->OnClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleAbilityButtonPressedSlot1);
    AbilityButton1->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (AbilityButton2) {
    AbilityButton2->OnClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleAbilityButtonPressedSlot2);
    AbilityButton2->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (AbilityButton3) {
    AbilityButton3->OnClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleAbilityButtonPressedSlot3);
    AbilityButton3->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (AbilityIcon1) {
    AbilityIcon1->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (AbilityIcon2) {
    AbilityIcon2->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (AbilityIcon3) {
    AbilityIcon3->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (PassiveAbilityIcon) {
    ApplyTooltipToWidget(PassiveAbilityIcon, FText::GetEmpty());
    PassiveAbilityIcon->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (EnemyPassiveAbilityIcon) {
    ApplyTooltipToWidget(EnemyPassiveAbilityIcon, FText::GetEmpty());
    EnemyPassiveAbilityIcon->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (AbilityLabel1) {
    AbilityLabel1->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (AbilityLabel2) {
    AbilityLabel2->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (AbilityLabel3) {
    AbilityLabel3->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (AbilityTriggeredText) {
    AbilityTriggeredText->SetText(FText::GetEmpty());
    AbilityTriggeredText->SetVisibility(ESlateVisibility::Collapsed);
  }

  ClearEnemyStatPanel();

  if (DiceResolutionPanel) {
    DiceResolutionPanel->OnResolutionComplete.AddDynamic(
        this, &UBattleHUDWidget::HandleDicePanelResolved);
    DiceResolutionPanel->OnDiceOutcomeRevealed.AddDynamic(
        this, &UBattleHUDWidget::HandleDiceOutcomeRevealed);
    TArray<UTexture2D *> DiceFaceTexturePtrs;
    DiceFaceTexturePtrs.Reserve(DiceFaceTextures.Num());
    for (const TObjectPtr<UTexture2D> &Texture : DiceFaceTextures) {
      DiceFaceTexturePtrs.Add(Texture.Get());
    }
    DiceResolutionPanel->SetDiceFaceTextures(DiceFaceTexturePtrs);
  }

  ApplyDiceResolutionPanelLayoutInternal(DefaultDiceResolutionPanelLayout);

  if (InitiativePromptText) {
    InitiativePromptText->SetText(FText::GetEmpty());
    InitiativePromptText->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (TerritoryText) {
    TerritoryText->SetText(FText::GetEmpty());
    TerritoryText->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (DiceRollerImage) {
    DiceRollerImage->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (UCombatFloaterPoolSubsystem *FloaterPool = ResolveFloaterPool()) {
    if (FloaterWidgetClass) {
      FloaterPool->FloaterWidgetClass = FloaterWidgetClass;
    }
  }

  CacheDefaultTextColor(HealthText);
  CacheDefaultTextColor(AttackText);
  CacheDefaultTextColor(CriticalDamageText);
  CacheDefaultTextColor(MoveText);
  CacheDefaultTextColor(ActionsText);
  CacheDefaultTextColor(StrengthText);
  CacheDefaultTextColor(DefenceText);
  CacheDefaultTextColor(AttackRangeText);
  CacheDefaultTextColor(AttackDiceText);
  CacheDefaultTextColor(EnemyHealthText);
  CacheDefaultTextColor(EnemyAttackText);
  CacheDefaultTextColor(EnemyCriticalDamageText);
  CacheDefaultTextColor(EnemyMoveText);
  CacheDefaultTextColor(EnemyActionsText);
  CacheDefaultTextColor(EnemyStrengthText);
  CacheDefaultTextColor(EnemyDefenceText);
  CacheDefaultTextColor(EnemyAttackRangeText);
  CacheDefaultTextColor(EnemyAttackDiceText);

  UpgradeStatIconTooltips();
}

void UBattleHUDWidget::NativeTick(const FGeometry &MyGeometry,
                                  float InDeltaTime) {
  Super::NativeTick(MyGeometry, InDeltaTime);
  UpdateCombatFloaters(InDeltaTime);
}

void UBattleHUDWidget::NativeDestruct() {
  if (DiceResolutionPanel) {
    DiceResolutionPanel->OnResolutionComplete.RemoveDynamic(
        this, &UBattleHUDWidget::HandleDicePanelResolved);
    DiceResolutionPanel->OnDiceOutcomeRevealed.RemoveDynamic(
        this, &UBattleHUDWidget::HandleDiceOutcomeRevealed);
  }

  if (BoundAbilityComponent.IsValid()) {
    if (USkaldAbilityComponent *Existing = BoundAbilityComponent.Get()) {
      Existing->OnAbilityStateChanged.RemoveDynamic(
          this, &UBattleHUDWidget::HandleAbilityComponentUpdated);
      Existing->OnAbilityTriggered.RemoveDynamic(
          this, &UBattleHUDWidget::HandleAbilityTriggered);
    }
    BoundAbilityComponent = nullptr;
  }

  PendingDiceResolutions.Reset();
  bDiceResolutionActive = false;
  ActiveDiceResolution = FBattleQueuedDiceResolution();
  ClearEnemyStatPanel();

  if (DiceRollerRenderTarget) {
    DiceRollerRenderTarget->OnCanvasRenderTargetUpdate.RemoveDynamic(
        this, &UBattleHUDWidget::HandleDiceRenderTargetUpdate);
    DiceRollerRenderTarget = nullptr;
  }

  if (AttackRollButton) {
    AttackRollButton->OnClicked.RemoveDynamic(
        this, &UBattleHUDWidget::HandleAttackRollPressed);
  }

  if (DisengageButton) {
    DisengageButton->OnClicked.RemoveDynamic(
        this, &UBattleHUDWidget::HandleDisengagePressed);
  }

  if (AbilityButton1) {
    AbilityButton1->OnClicked.RemoveDynamic(
        this, &UBattleHUDWidget::HandleAbilityButtonPressedSlot1);
  }
  if (AbilityButton2) {
    AbilityButton2->OnClicked.RemoveDynamic(
        this, &UBattleHUDWidget::HandleAbilityButtonPressedSlot2);
  }
  if (AbilityButton3) {
    AbilityButton3->OnClicked.RemoveDynamic(
        this, &UBattleHUDWidget::HandleAbilityButtonPressedSlot3);
  }

  if (ASkaldPlayerController *SkaldController =
          Cast<ASkaldPlayerController>(GetOwningPlayer())) {
    SkaldController->OnSelectedFighterChanged.RemoveDynamic(
        this, &UBattleHUDWidget::HandleSelectedFighterChanged);
  }

  ClearLockedInFighterList();
  ClearEnemyLockedInFighterList();

  while (ActiveFloaters.Num() > 0) {
    ReleaseFloaterAtIndex(ActiveFloaters.Num() - 1);
  }
  CachedFloaterPool.Reset();

  ClearHealthTextHold();

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AbilityTriggerHideTimer);
  }
  HideAbilityTriggeredText();

  Super::NativeDestruct();
}

void UBattleHUDWidget::SetLockedInFighters(
    const TArray<AFighterPawn *> &Fighters) {
  if (!ScrollBox_LockedInFightersList) {
    UE_LOG(LogSkaldUI, Verbose,
           TEXT("[BattleHUD] Locked-in fighter list scroll box missing."));
    return;
  }

  if (!LockedInFighterEntryClass) {
    UE_LOG(LogSkaldUI, Warning,
           TEXT("[BattleHUD] LockedInFighterEntryClass not set. Configure the HUD widget defaults."));
    return;
  }

  PruneInvalidLockedInEntries();

  LockedInFighterOrder.Reset();
  KnownFriendlyFighters.Reset();
  ScrollBox_LockedInFightersList->ClearChildren();

  TSet<AFighterPawn *> DesiredSet;
  for (AFighterPawn *Fighter : Fighters) {
    if (!Fighter) {
      continue;
    }

    DesiredSet.Add(Fighter);
    LockedInFighterOrder.Add(Fighter);
    KnownFriendlyFighters.Add(Fighter);

    if (ULockedInFighterEntryWidget *Entry =
            FindOrCreateLockedInEntry(Fighter)) {
      Entry->SetFighter(Fighter);
      Entry->SetIsActive(ActiveLockedInFighter.IsValid() &&
                         ActiveLockedInFighter.Get() == Fighter);
      Entry->SetIsSelected(HighlightedLockedInFighter.IsValid() &&
                           HighlightedLockedInFighter.Get() == Fighter);
      Entry->RefreshTurnState();
      ScrollBox_LockedInFightersList->AddChild(Entry);
    }
  }

  for (auto It = LockedInFighterEntries.CreateIterator(); It; ++It) {
    AFighterPawn *Fighter = It->Key.Get();
    if (!Fighter || !DesiredSet.Contains(Fighter)) {
      if (It->Value) {
        It->Value->RemoveFromParent();
        It->Value->ResetEntry();
      }
      KnownFriendlyFighters.Remove(It->Key);
      It.RemoveCurrent();
    }
  }

  if (HighlightedLockedInFighter.IsValid() &&
      !DesiredSet.Contains(HighlightedLockedInFighter.Get())) {
    HighlightedLockedInFighter.Reset();
  }

  if (ActiveLockedInFighter.IsValid() &&
      !DesiredSet.Contains(ActiveLockedInFighter.Get())) {
    ActiveLockedInFighter.Reset();
  }
}

void UBattleHUDWidget::SetEnemyLockedInFighters(
    const TArray<AFighterPawn *> &Fighters) {
  if (!ScrollBox_EnemyLockedInFightersList) {
    UE_LOG(LogSkaldUI, Verbose,
           TEXT("[BattleHUD] Enemy locked-in fighter list scroll box missing."));
    return;
  }

  if (!LockedInFighterEntryClass) {
    UE_LOG(LogSkaldUI, Warning,
           TEXT("[BattleHUD] LockedInFighterEntryClass not set. Configure the HUD widget defaults."));
    return;
  }

  PruneInvalidEnemyLockedInEntries();

  EnemyLockedInFighterOrder.Reset();
  KnownEnemyFighters.Reset();
  ScrollBox_EnemyLockedInFightersList->ClearChildren();

  TSet<AFighterPawn *> DesiredSet;
  for (AFighterPawn *Fighter : Fighters) {
    if (!Fighter) {
      continue;
    }

    DesiredSet.Add(Fighter);
    EnemyLockedInFighterOrder.Add(Fighter);
    KnownEnemyFighters.Add(Fighter);

    if (ULockedInFighterEntryWidget *Entry =
            FindOrCreateEnemyLockedInEntry(Fighter)) {
      Entry->SetFighter(Fighter);
      Entry->SetIsActive(ActiveEnemyLockedInFighter.IsValid() &&
                         ActiveEnemyLockedInFighter.Get() == Fighter);
      Entry->SetIsSelected(HighlightedEnemyLockedInFighter.IsValid() &&
                           HighlightedEnemyLockedInFighter.Get() == Fighter);
      Entry->RefreshTurnState();
      ScrollBox_EnemyLockedInFightersList->AddChild(Entry);
    }
  }

  for (auto It = EnemyLockedInFighterEntries.CreateIterator(); It; ++It) {
    AFighterPawn *Fighter = It->Key.Get();
    if (!Fighter || !DesiredSet.Contains(Fighter)) {
      if (It->Value) {
        It->Value->RemoveFromParent();
        It->Value->ResetEntry();
      }
      KnownEnemyFighters.Remove(It->Key);
      It.RemoveCurrent();
    }
  }

  if (HighlightedEnemyLockedInFighter.IsValid() &&
      !DesiredSet.Contains(HighlightedEnemyLockedInFighter.Get())) {
    HighlightedEnemyLockedInFighter.Reset();
  }

  if (ActiveEnemyLockedInFighter.IsValid() &&
      !DesiredSet.Contains(ActiveEnemyLockedInFighter.Get())) {
    ActiveEnemyLockedInFighter.Reset();
  }
}

void UBattleHUDWidget::ClearLockedInFighterList() {
  if (ScrollBox_LockedInFightersList) {
    ScrollBox_LockedInFightersList->ClearChildren();
  }

  for (auto &Pair : LockedInFighterEntries) {
    if (Pair.Value) {
      Pair.Value->ResetEntry();
    }
  }
  LockedInFighterEntries.Reset();
  LockedInFighterOrder.Reset();
  HighlightedLockedInFighter.Reset();
  ActiveLockedInFighter.Reset();
  KnownFriendlyFighters.Reset();
}

void UBattleHUDWidget::ClearEnemyLockedInFighterList() {
  if (ScrollBox_EnemyLockedInFightersList) {
    ScrollBox_EnemyLockedInFightersList->ClearChildren();
  }

  for (auto &Pair : EnemyLockedInFighterEntries) {
    if (Pair.Value) {
      Pair.Value->ResetEntry();
    }
  }
  EnemyLockedInFighterEntries.Reset();
  EnemyLockedInFighterOrder.Reset();
  HighlightedEnemyLockedInFighter.Reset();
  ActiveEnemyLockedInFighter.Reset();
  KnownEnemyFighters.Reset();
}

void UBattleHUDWidget::SetHighlightedLockedInFighter(AFighterPawn *Fighter) {
  HighlightedLockedInFighter = Fighter;
  PruneInvalidLockedInEntries();

  for (auto &Pair : LockedInFighterEntries) {
    if (ULockedInFighterEntryWidget *Entry = Pair.Value) {
      const bool bIsSelected = (Pair.Key.Get() == Fighter && Fighter != nullptr);
      Entry->SetIsSelected(bIsSelected);
    }
  }
}

void UBattleHUDWidget::SetHighlightedEnemyLockedInFighter(AFighterPawn *Fighter) {
  HighlightedEnemyLockedInFighter = Fighter;
  PruneInvalidEnemyLockedInEntries();

  for (auto &Pair : EnemyLockedInFighterEntries) {
    if (ULockedInFighterEntryWidget *Entry = Pair.Value) {
      const bool bIsSelected = (Pair.Key.Get() == Fighter && Fighter != nullptr);
      Entry->SetIsSelected(bIsSelected);
    }
  }
}

void UBattleHUDWidget::HandleEnemyLockedInEntryClicked(AFighterPawn *Fighter) {
  if (!Fighter) {
    return;
  }

  SetHighlightedEnemyLockedInFighter(Fighter);

  if (ASkaldPlayerController *SkaldController =
          Cast<ASkaldPlayerController>(GetOwningPlayer())) {
    SkaldController->RequestEnemyLockedInEntrySelection(Fighter);
  }
}

void UBattleHUDWidget::HandleSelectedFighterChanged(AFighterPawn *Fighter) {
  const bool bIsFriendly = IsKnownFriendlyFighter(Fighter);
  const bool bHasEnemyEntry = IsKnownEnemyFighter(Fighter);

  if (bIsFriendly) {
    SetHighlightedLockedInFighter(Fighter);
    SetHighlightedEnemyLockedInFighter(nullptr);
    return;
  }

  SetHighlightedLockedInFighter(nullptr);

  if (Fighter) {
    SetHighlightedEnemyLockedInFighter(bHasEnemyEntry ? Fighter : nullptr);
    UpdateEnemyStatPanel(Fighter);
  } else {
    SetHighlightedEnemyLockedInFighter(nullptr);
    if (!bDiceResolutionActive && !ActiveEnemyLockedInFighter.IsValid()) {
      ClearEnemyStatPanel();
    }
  }
}

void UBattleHUDWidget::SetActiveLockedInFighter(AFighterPawn *Fighter) {
  ActiveLockedInFighter = Fighter;
  PruneInvalidLockedInEntries();

  for (auto &Pair : LockedInFighterEntries) {
    if (ULockedInFighterEntryWidget *Entry = Pair.Value) {
      const bool bIsActive = (Pair.Key.Get() == Fighter && Fighter != nullptr);
      Entry->SetIsActive(bIsActive);
      if (bIsActive) {
        Entry->RefreshTurnState();
      }
    }
  }
}

void UBattleHUDWidget::SetActiveEnemyLockedInFighter(AFighterPawn *Fighter) {
  ActiveEnemyLockedInFighter = Fighter;
  PruneInvalidEnemyLockedInEntries();

  for (auto &Pair : EnemyLockedInFighterEntries) {
    if (ULockedInFighterEntryWidget *Entry = Pair.Value) {
      const bool bIsActive = (Pair.Key.Get() == Fighter && Fighter != nullptr);
      Entry->SetIsActive(bIsActive);
      if (bIsActive) {
        Entry->RefreshTurnState();
      }
    }
  }

  if (Fighter) {
    UpdateEnemyStatPanel(Fighter);
  } else if (!bDiceResolutionActive) {
    ClearEnemyStatPanel();
  }
}

void UBattleHUDWidget::RefreshLockedInFighterTurnStates() {
  PruneInvalidLockedInEntries();
  for (auto &Pair : LockedInFighterEntries) {
    if (ULockedInFighterEntryWidget *Entry = Pair.Value) {
      Entry->RefreshTurnState();
    }
  }
  RefreshEnemyLockedInFighterTurnStates();
}

void UBattleHUDWidget::RefreshEnemyLockedInFighterTurnStates() {
  PruneInvalidEnemyLockedInEntries();
  for (auto &Pair : EnemyLockedInFighterEntries) {
    if (ULockedInFighterEntryWidget *Entry = Pair.Value) {
      Entry->RefreshTurnState();
    }
  }
}

void UBattleHUDWidget::PruneInvalidLockedInEntries() {
  for (auto It = LockedInFighterEntries.CreateIterator(); It; ++It) {
    const bool bEntryValid = It->Key.IsValid() && It->Value != nullptr;
    if (!bEntryValid) {
      if (It->Value) {
        It->Value->RemoveFromParent();
        It->Value->ResetEntry();
      }
      KnownFriendlyFighters.Remove(It->Key);
      It.RemoveCurrent();
    }
  }

  LockedInFighterOrder.RemoveAll([](const TWeakObjectPtr<AFighterPawn> &Ptr) {
    return !Ptr.IsValid();
  });

  for (auto It = KnownFriendlyFighters.CreateIterator(); It; ++It) {
    if (!It->IsValid()) {
      It.RemoveCurrent();
    }
  }
}

void UBattleHUDWidget::PruneInvalidEnemyLockedInEntries() {
  for (auto It = EnemyLockedInFighterEntries.CreateIterator(); It; ++It) {
    const bool bEntryValid = It->Key.IsValid() && It->Value != nullptr;
    if (!bEntryValid) {
      if (It->Value) {
        It->Value->RemoveFromParent();
        It->Value->ResetEntry();
      }
      KnownEnemyFighters.Remove(It->Key);
      It.RemoveCurrent();
    }
  }

  EnemyLockedInFighterOrder.RemoveAll([](const TWeakObjectPtr<AFighterPawn> &Ptr) {
    return !Ptr.IsValid();
  });

  for (auto It = KnownEnemyFighters.CreateIterator(); It; ++It) {
    if (!It->IsValid()) {
      It.RemoveCurrent();
    }
  }
}

ULockedInFighterEntryWidget *
UBattleHUDWidget::FindLockedInEntry(AFighterPawn *Fighter) const {
  if (!Fighter) {
    return nullptr;
  }

  for (const auto &Pair : LockedInFighterEntries) {
    if (Pair.Key.Get() == Fighter) {
      return Pair.Value;
    }
  }
  return nullptr;
}

ULockedInFighterEntryWidget *
UBattleHUDWidget::FindEnemyLockedInEntry(AFighterPawn *Fighter) const {
  if (!Fighter) {
    return nullptr;
  }

  for (const auto &Pair : EnemyLockedInFighterEntries) {
    if (Pair.Key.Get() == Fighter) {
      return Pair.Value;
    }
  }
  return nullptr;
}

ULockedInFighterEntryWidget *
UBattleHUDWidget::FindOrCreateLockedInEntry(AFighterPawn *Fighter) {
  if (!Fighter) {
    return nullptr;
  }

  if (ULockedInFighterEntryWidget *Existing = FindLockedInEntry(Fighter)) {
    return Existing;
  }

  if (!LockedInFighterEntryClass) {
    return nullptr;
  }

  ULocalPlayer *OwningLocalPlayer = GetOwningLocalPlayer();
  if (!OwningLocalPlayer) {
    UE_LOG(LogSkaldUI, Warning,
           TEXT("BattleHUD: missing owning LocalPlayer; skipping locked-in fighter entry widget creation."));
    return nullptr;
  }
  ULockedInFighterEntryWidget *NewEntry =
      CreateWidget<ULockedInFighterEntryWidget>(GetOwningPlayer(),
                                                LockedInFighterEntryClass);
  if (NewEntry) {
    NewEntry->OnEntryClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleLockedInEntryClicked);
    NewEntry->OnEntryRemoved.AddDynamic(
        this, &UBattleHUDWidget::HandleLockedInEntryRemoved);
    LockedInFighterEntries.Add(Fighter, NewEntry);
    return NewEntry;
  }

  return nullptr;
}

ULockedInFighterEntryWidget *
UBattleHUDWidget::FindOrCreateEnemyLockedInEntry(AFighterPawn *Fighter) {
  if (!Fighter) {
    return nullptr;
  }

  if (ULockedInFighterEntryWidget *Existing = FindEnemyLockedInEntry(Fighter)) {
    return Existing;
  }

  if (!LockedInFighterEntryClass) {
    return nullptr;
  }

  ULocalPlayer *OwningLocalPlayer = GetOwningLocalPlayer();
  if (!OwningLocalPlayer) {
    UE_LOG(LogSkaldUI, Warning,
           TEXT("BattleHUD: missing owning LocalPlayer; skipping enemy locked-in entry widget creation."));
    return nullptr;
  }
  ULockedInFighterEntryWidget *NewEntry =
      CreateWidget<ULockedInFighterEntryWidget>(GetOwningPlayer(),
                                                LockedInFighterEntryClass);
  if (NewEntry) {
    NewEntry->OnEntryClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleEnemyLockedInEntryClicked);
    NewEntry->OnEntryRemoved.AddDynamic(
        this, &UBattleHUDWidget::HandleEnemyLockedInEntryRemoved);
    EnemyLockedInFighterEntries.Add(Fighter, NewEntry);
    return NewEntry;
  }

  return nullptr;
}

void UBattleHUDWidget::HandleLockedInEntryClicked(AFighterPawn *Fighter) {
  if (!Fighter) {
    return;
  }

  SetHighlightedLockedInFighter(Fighter);

  if (ASkaldPlayerController *SkaldController =
          Cast<ASkaldPlayerController>(GetOwningPlayer())) {
    SkaldController->RequestLockedInEntrySelection(Fighter);
  }

  OnLockedInFighterEntrySelected.Broadcast(Fighter);
}

void UBattleHUDWidget::HandleLockedInEntryRemoved(AFighterPawn *Fighter) {
  RemoveLockedInEntry(Fighter);
}

void UBattleHUDWidget::RemoveLockedInEntry(AFighterPawn *Fighter) {
  if (!Fighter) {
    return;
  }

  for (auto It = LockedInFighterEntries.CreateIterator(); It; ++It) {
    if (It->Key.Get() == Fighter) {
      if (It->Value) {
        It->Value->RemoveFromParent();
        It->Value->ResetEntry();
      }
      It.RemoveCurrent();
      break;
    }
  }

  KnownFriendlyFighters.Remove(Fighter);

  LockedInFighterOrder.RemoveAll([Fighter](const TWeakObjectPtr<AFighterPawn> &Ptr) {
    return Ptr.Get() == Fighter;
  });

  if (HighlightedLockedInFighter.Get() == Fighter) {
    HighlightedLockedInFighter.Reset();
  }
  if (ActiveLockedInFighter.Get() == Fighter) {
    ActiveLockedInFighter.Reset();
  }

  if (ScrollBox_LockedInFightersList) {
    ScrollBox_LockedInFightersList->ClearChildren();
    for (const TWeakObjectPtr<AFighterPawn> &Ptr : LockedInFighterOrder) {
      if (AFighterPawn *OrderedFighter = Ptr.Get()) {
        if (ULockedInFighterEntryWidget *Entry =
                FindLockedInEntry(OrderedFighter)) {
          ScrollBox_LockedInFightersList->AddChild(Entry);
        }
      }
    }
  }
}

void UBattleHUDWidget::HandleEnemyLockedInEntryRemoved(AFighterPawn *Fighter) {
  RemoveEnemyLockedInEntry(Fighter);
}

void UBattleHUDWidget::RemoveEnemyLockedInEntry(AFighterPawn *Fighter) {
  if (!Fighter) {
    return;
  }

  for (auto It = EnemyLockedInFighterEntries.CreateIterator(); It; ++It) {
    if (It->Key.Get() == Fighter) {
      if (It->Value) {
        It->Value->RemoveFromParent();
        It->Value->ResetEntry();
      }
      It.RemoveCurrent();
      break;
    }
  }

  KnownEnemyFighters.Remove(Fighter);

  EnemyLockedInFighterOrder.RemoveAll(
      [Fighter](const TWeakObjectPtr<AFighterPawn> &Ptr) {
        return Ptr.Get() == Fighter;
      });

  if (HighlightedEnemyLockedInFighter.Get() == Fighter) {
    HighlightedEnemyLockedInFighter.Reset();
  }
  if (ActiveEnemyLockedInFighter.Get() == Fighter) {
    ActiveEnemyLockedInFighter.Reset();
  }

  if (ScrollBox_EnemyLockedInFightersList) {
    ScrollBox_EnemyLockedInFightersList->ClearChildren();
    for (const TWeakObjectPtr<AFighterPawn> &Ptr : EnemyLockedInFighterOrder) {
      if (AFighterPawn *OrderedFighter = Ptr.Get()) {
        if (ULockedInFighterEntryWidget *Entry =
                FindEnemyLockedInEntry(OrderedFighter)) {
          ScrollBox_EnemyLockedInFightersList->AddChild(Entry);
        }
      }
    }
  }
}

void UBattleHUDWidget::ShowInitiativePrompt(const FText &PromptText) {
  if (InitiativePromptText) {
    InitiativePromptText->SetText(PromptText);
    InitiativePromptText->SetVisibility(ESlateVisibility::HitTestInvisible);
  }
  if (RollInitiativeButton) {
    RollInitiativeButton->SetVisibility(ESlateVisibility::Collapsed);
    RollInitiativeButton->SetIsEnabled(false);
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(InitiativeRollButtonDelayTimer);
    World->GetTimerManager().SetTimer(
        InitiativeRollButtonDelayTimer, this,
        &UBattleHUDWidget::RevealInitiativeRollButton, 1.0f, false);
  }
}

void UBattleHUDWidget::HideInitiativePrompt() {
  if (InitiativePromptText) {
    InitiativePromptText->SetText(FText::GetEmpty());
    InitiativePromptText->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (RollInitiativeButton) {
    RollInitiativeButton->SetVisibility(ESlateVisibility::Collapsed);
    RollInitiativeButton->SetIsEnabled(true);
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(InitiativeRollButtonDelayTimer);
  }
}

void UBattleHUDWidget::RefreshStats() { UpdateStatPanel(); }

void UBattleHUDWidget::BindToFighter(AFighterPawn *Fighter) {
  if (BoundFighter) {
    BoundFighter->OnHealthDisplayUpdated.RemoveDynamic(
        this, &UBattleHUDWidget::HandleHealthChanged);
    BoundFighter->OnActionsChanged.RemoveDynamic(
        this, &UBattleHUDWidget::HandleActionsChanged);
    BoundFighter->OnEngagementChanged.RemoveDynamic(
        this, &UBattleHUDWidget::HandleFighterEngagementChanged);
  }

  if (BoundAbilityComponent.IsValid()) {
    if (USkaldAbilityComponent *Existing = BoundAbilityComponent.Get()) {
      Existing->OnAbilityStateChanged.RemoveDynamic(
          this, &UBattleHUDWidget::HandleAbilityComponentUpdated);
      Existing->OnAbilityTriggered.RemoveDynamic(
          this, &UBattleHUDWidget::HandleAbilityTriggered);
    }
    BoundAbilityComponent = nullptr;
  }

  if (BoundFighter && BoundFighter != Fighter) {
    ClearHealthTextHold();
  }

  const bool bShouldDisplayAsFriendly = IsFriendlyCandidate(Fighter);

  BoundFighter = Fighter;
  bBoundFighterIsFriendly = bShouldDisplayAsFriendly;

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AbilityTriggerHideTimer);
  }
  HideAbilityTriggeredText();

  if (BoundFighter && bBoundFighterIsFriendly) {
    BoundFighter->OnHealthDisplayUpdated.AddDynamic(
        this, &UBattleHUDWidget::HandleHealthChanged);
    BoundFighter->OnActionsChanged.AddDynamic(
        this, &UBattleHUDWidget::HandleActionsChanged);
    BoundFighter->OnEngagementChanged.AddDynamic(
        this, &UBattleHUDWidget::HandleFighterEngagementChanged);

    if (USkaldAbilityComponent *AbilityComp =
            BoundFighter->GetAbilityComponent()) {
      BoundAbilityComponent = AbilityComp;
      AbilityComp->OnAbilityStateChanged.AddDynamic(
          this, &UBattleHUDWidget::HandleAbilityComponentUpdated);
      AbilityComp->OnAbilityTriggered.AddDynamic(
          this, &UBattleHUDWidget::HandleAbilityTriggered);
    }

    UpdateStatPanel();
  } else {
    BoundAbilityComponent = nullptr;

    if (!BoundFighter) {
      ClearPrimaryStatPanel();
      ClearEnemyStatPanel();
    } else {
      UpdateStatPanel();
    }
  }

  RefreshAbilityDisplay();
  UpdateActionButtonVisibility();
  HandleFighterEngagementChanged(BoundFighter ? BoundFighter->IsEngaged() : false);
}

void UBattleHUDWidget::HandleMovePressed() {
  OnMovePressed.Broadcast();
  bAttackSelected = false;
  bDisengageSelected = false;
  if (!BoundFighter || BoundFighter->IsEngaged()) {
    return;
  }
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    if (bMoveSelected) {
      Grid->ClearHighlights();
      bMoveSelected = false;
    } else {
      Grid->HighlightMovement(BoundFighter);
      bMoveSelected = true;
    }
  }
}

void UBattleHUDWidget::HandleDisengagePressed() {
  OnDisengagePressed.Broadcast();
  bMoveSelected = false;
  bAttackSelected = false;
  if (!BoundFighter || !BoundFighter->IsEngaged()) {
    return;
  }
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    if (bDisengageSelected) {
      Grid->ClearHighlights();
      bDisengageSelected = false;
    } else {
      const int32 DisengageRange = BoundFighter->GetDisengageRange();
      if (DisengageRange > 0) {
        Grid->HighlightDisengage(BoundFighter, DisengageRange);
        bDisengageSelected = true;
      }
    }
  }
}

void UBattleHUDWidget::HandleAttackPressed() {
  OnAttackPressed.Broadcast();
  bMoveSelected = false;
  bDisengageSelected = false;
  if (!BoundFighter) {
    return;
  }
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    if (bAttackSelected) {
      Grid->ClearHighlights();
      bAttackSelected = false;
    } else {
      Grid->HighlightAttack(BoundFighter);
      bAttackSelected = true;
    }
  }
}

void UBattleHUDWidget::HandleActivatePressed() {
  OnActivatePressed.Broadcast();
  ClearCommandPreviews();
}

void UBattleHUDWidget::HandleEndTurnPressed() {
  OnEndTurnPressed.Broadcast();
  ClearCommandPreviews();
}

void UBattleHUDWidget::HandleInitiativeRollPressed() {
  if (RollInitiativeButton) {
    RollInitiativeButton->SetIsEnabled(false);
  }
  HideInitiativePrompt();
  OnInitiativeRollRequested.Broadcast();
}

void UBattleHUDWidget::HandleAttackRollPressed() {
  if (bManualDiceResolutionActive) {
    SetAttackRollButtonVisibility(false);

    if (!DiceResolutionPanel) {
      return;
    }

    const bool bAdvanced = DiceResolutionPanel->AdvanceManualReveal();

    if (!bAdvanced && bManualDiceResolutionActive) {
      SetAttackRollButtonVisibility(true);
    }

    return;
  }

  if (!bManualAttackRollPromptActive) {
    return;
  }

  SetAttackRollButtonVisibility(false);

  OnAttackRollRequested.Broadcast();
}

void UBattleHUDWidget::HandleAbilityButtonPressedSlot1() {
  OnAbilitySlotPressed.Broadcast(ESkaldAbilitySlot::Ability1);
}

void UBattleHUDWidget::HandleAbilityButtonPressedSlot2() {
  OnAbilitySlotPressed.Broadcast(ESkaldAbilitySlot::Ability2);
}

void UBattleHUDWidget::HandleAbilityButtonPressedSlot3() {
  OnAbilitySlotPressed.Broadcast(ESkaldAbilitySlot::Ability3);
}

void UBattleHUDWidget::RevealInitiativeRollButton() {
  if (RollInitiativeButton) {
    RollInitiativeButton->SetVisibility(ESlateVisibility::Visible);
    RollInitiativeButton->SetIsEnabled(true);
  }
}

void UBattleHUDWidget::HandleHealthChanged(int32 NewHealth) {
  if (!bBoundFighterIsFriendly) {
    return;
  }

  if (bHealthTextHoldActive && HeldHealthTextFighter.IsValid() &&
      HeldHealthTextFighter.Get() == BoundFighter) {
    bHasPendingHealthTextValue = true;
    PendingHealthTextValue = FMath::Max(0, NewHealth);
    return;
  }

  ApplyStatText(HealthText, NewHealth, 0);
}

void UBattleHUDWidget::HandleEnemyHealthDisplayChanged(int32 NewHealth) {
  if (!DisplayedEnemyStatFighter.IsValid()) {
    return;
  }

  ApplyStatTextWithVisibility(EnemyHealthText, NewHealth, 0);
}

void UBattleHUDWidget::HandleActionsChanged(int32 NewActions) {
  if (!bBoundFighterIsFriendly) {
    return;
  }

  ApplyStatText(ActionsText, NewActions, 0);

  UpdateActionButtonVisibility();
  RefreshAbilityDisplay();
}

void UBattleHUDWidget::HandleFighterEngagementChanged(bool /*bEngaged*/) {
  ClearCommandPreviews();
  UpdateActionButtonVisibility();
}

void UBattleHUDWidget::HandleAbilityComponentUpdated(
    USkaldAbilityComponent *AbilityComponent) {
  if (!BoundAbilityComponent.IsValid() ||
      BoundAbilityComponent.Get() != AbilityComponent) {
    return;
  }

  RefreshAbilityDisplay();
}

void UBattleHUDWidget::UpdateStatPanel() {
  if (BoundFighter && bBoundFighterIsFriendly) {
    ApplyPrimaryFighterDisplay(BoundFighter);
  } else if (!DisplayedFriendlyStatFighter.IsValid()) {
    ClearPrimaryStatPanel();
  }

  UpdateActionButtonVisibility();
}

void UBattleHUDWidget::RefreshEnemyDisplayAfterResolution() {
  AFighterPawn *DesiredEnemy = nullptr;

  if (ManualAttackRollTarget.IsValid()) {
    DesiredEnemy = ManualAttackRollTarget.Get();
  } else if (HighlightedEnemyLockedInFighter.IsValid()) {
    DesiredEnemy = HighlightedEnemyLockedInFighter.Get();
  } else if (ActiveEnemyLockedInFighter.IsValid()) {
    DesiredEnemy = ActiveEnemyLockedInFighter.Get();
  } else if (DisplayedEnemyStatFighter.IsValid()) {
    DesiredEnemy = DisplayedEnemyStatFighter.Get();
  }

  UpdateEnemyStatPanel(DesiredEnemy);
}

void UBattleHUDWidget::UpdateEnemyStatFighterBinding(AFighterPawn *NewFighter) {
  AFighterPawn *CurrentDisplayed = DisplayedEnemyStatFighter.Get();
  if (CurrentDisplayed == NewFighter) {
    return;
  }

  if (CurrentDisplayed) {
    CurrentDisplayed->OnHealthDisplayUpdated.RemoveDynamic(
        this, &UBattleHUDWidget::HandleEnemyHealthDisplayChanged);
  }

  DisplayedEnemyStatFighter = NewFighter;

  if (NewFighter) {
    NewFighter->OnHealthDisplayUpdated.AddDynamic(
        this, &UBattleHUDWidget::HandleEnemyHealthDisplayChanged);
  }
}

void UBattleHUDWidget::UpdateEnemyStatPanel(AFighterPawn *Fighter) {
  AFighterPawn *ResolvedFighter = Fighter;
  if (ResolvedFighter && IsFriendlyCandidate(ResolvedFighter)) {
    ResolvedFighter = nullptr;
  }

  if (!ResolvedFighter) {
    ClearEnemyStatPanel();
    return;
  }

  UpdateEnemyStatFighterBinding(ResolvedFighter);
  KnownEnemyFighters.Add(ResolvedFighter);

  const FSkaldAbilityStatDelta NetDelta = GatherNetStatDelta(ResolvedFighter);
  const int32 AttackDamageDelta =
      ResolveAttackDamageDelta(ResolvedFighter, NetDelta);
  const int32 CriticalDamageDelta =
      AttackDamageDelta + NetDelta.CriticalBonusDamage;

  ApplyStatTextWithVisibility(EnemyHealthText,
                              ResolvedFighter->Stats.Health, 0);
  ApplyStatTextWithVisibility(EnemyAttackText,
                              ResolvedFighter->Stats.AttackDamage,
                              AttackDamageDelta);

  if (EnemyCriticalDamageText) {
    const int32 CriticalDamage = ResolvedFighter->Stats.AttackDamage +
                                 ResolvedFighter->Stats.CriticalBonusDamage;
    ApplyStatTextWithVisibility(EnemyCriticalDamageText, CriticalDamage,
                                CriticalDamageDelta);
  }

  ApplyStatTextWithVisibility(EnemyMoveText, ResolvedFighter->Stats.Movement,
                              NetDelta.Movement);
  ApplyStatTextWithVisibility(EnemyActionsText,
                              ResolvedFighter->ActionsRemaining, 0);
  ApplyStatTextWithVisibility(EnemyStrengthText,
                              ResolvedFighter->Stats.Strength,
                              NetDelta.Strength);
  ApplyStatTextWithVisibility(EnemyDefenceText,
                              ResolvedFighter->Stats.Defence,
                              NetDelta.Defence);
  ApplyStatTextWithVisibility(EnemyAttackRangeText,
                              ResolvedFighter->Stats.AttackRange,
                              NetDelta.AttackRange);
  ApplyStatTextWithVisibility(EnemyAttackDiceText,
                              ResolvedFighter->Stats.AttackDice,
                              NetDelta.AttackDice);

  if (EnemyFighterNameText) {
    EnemyFighterNameText->SetText(
        FText::FromName(ResolvedFighter->GetFighterId()));
    EnemyFighterNameText->SetVisibility(ESlateVisibility::HitTestInvisible);
  }

  if (EnemyFighterImage) {
    if (UTexture2D *PortraitTexture = ResolvedFighter->GetPortraitTexture()) {
      EnemyFighterImage->SetBrushFromTexture(PortraitTexture);
      EnemyFighterImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    } else {
      EnemyFighterImage->SetBrushFromTexture(nullptr);
      EnemyFighterImage->SetVisibility(ESlateVisibility::Collapsed);
    }
  }

  const FSkaldAbilityDefinition TieredAbility =
      ResolveTieredAbilityDefinition(ResolvedFighter);
  UpdateEnemyAbilityText(TieredAbility);

  const FSkaldAbilityDefinition PassiveAbility =
      ResolvePassiveAbilityDefinition(ResolvedFighter);
  UpdatePassiveAbilityIcon(EnemyPassiveAbilityIcon, PassiveAbility);
}

void UBattleHUDWidget::ClearEnemyStatPanel() {
  if (bManualAttackRollPromptActive &&
      (ManualAttackRollTarget.IsValid() || DisplayedEnemyStatFighter.IsValid())) {
    return;
  }

  UpdateEnemyStatFighterBinding(nullptr);

  const auto ClearTextAndHide = [](UTextBlock *Widget) {
    if (Widget) {
      Widget->SetText(FText::GetEmpty());
      Widget->SetVisibility(ESlateVisibility::Collapsed);
    }
  };

  ClearTextAndHide(EnemyHealthText);
  ClearTextAndHide(EnemyAttackText);
  ClearTextAndHide(EnemyCriticalDamageText);
  ClearTextAndHide(EnemyMoveText);
  ClearTextAndHide(EnemyActionsText);
  ClearTextAndHide(EnemyStrengthText);
  ClearTextAndHide(EnemyDefenceText);
  ClearTextAndHide(EnemyAttackRangeText);
  ClearTextAndHide(EnemyAttackDiceText);
  ClearTextAndHide(EnemyFighterNameText);

  ResetStatTextColor(EnemyHealthText);
  ResetStatTextColor(EnemyAttackText);
  ResetStatTextColor(EnemyCriticalDamageText);
  ResetStatTextColor(EnemyMoveText);
  ResetStatTextColor(EnemyActionsText);
  ResetStatTextColor(EnemyStrengthText);
  ResetStatTextColor(EnemyDefenceText);
  ResetStatTextColor(EnemyAttackRangeText);
  ResetStatTextColor(EnemyAttackDiceText);

  if (EnemyFighterImage) {
    EnemyFighterImage->SetBrushFromTexture(nullptr);
    EnemyFighterImage->SetVisibility(ESlateVisibility::Collapsed);
  }

  UpdateEnemyAbilityText(FSkaldAbilityDefinition());
  UpdatePassiveAbilityIcon(EnemyPassiveAbilityIcon, FSkaldAbilityDefinition());
}

void UBattleHUDWidget::UpdateEnemyAbilityText(
    const FSkaldAbilityDefinition &Definition) {
  if (!EnemyAbilityText) {
    return;
  }

  if (!Definition.IsValid()) {
    EnemyAbilityText->SetText(FText::GetEmpty());
    EnemyAbilityText->SetVisibility(ESlateVisibility::Collapsed);
    ApplyTooltipToWidget(EnemyAbilityText, FText::GetEmpty());
    return;
  }

  EnemyAbilityText->SetText(Definition.AbilityName);
  EnemyAbilityText->SetVisibility(ESlateVisibility::HitTestInvisible);

  const FText TooltipText = !Definition.AbilityDescription.IsEmpty()
                                ? Definition.AbilityDescription
                                : BuildAbilityTooltipText(Definition);
  ApplyTooltipToWidget(EnemyAbilityText, TooltipText);
}

void UBattleHUDWidget::ApplyPrimaryFighterDisplay(AFighterPawn *Fighter) {
  DisplayedFriendlyStatFighter = Fighter;
  if (!Fighter) {
    ClearPrimaryStatPanel();
    return;
  }

  const FSkaldAbilityStatDelta NetDelta = GatherNetStatDelta(Fighter);
  const int32 AttackDamageDelta = ResolveAttackDamageDelta(Fighter, NetDelta);
  const int32 CriticalDamageDelta = AttackDamageDelta + NetDelta.CriticalBonusDamage;

  ApplyStatText(HealthText, Fighter->Stats.Health, 0);
  ApplyStatText(AttackText, Fighter->Stats.AttackDamage, AttackDamageDelta);
  ApplyStatText(MoveText, Fighter->Stats.Movement, NetDelta.Movement);
  ApplyStatText(ActionsText, Fighter->ActionsRemaining, 0);
  ApplyStatText(StrengthText, Fighter->Stats.Strength, NetDelta.Strength);
  ApplyStatText(DefenceText, Fighter->Stats.Defence, NetDelta.Defence);
  ApplyStatText(AttackRangeText, Fighter->Stats.AttackRange, NetDelta.AttackRange);
  ApplyStatText(AttackDiceText, Fighter->Stats.AttackDice, NetDelta.AttackDice);

  if (CriticalDamageText) {
    const int32 CriticalDamage =
        Fighter->Stats.AttackDamage + Fighter->Stats.CriticalBonusDamage;
    ApplyStatText(CriticalDamageText, CriticalDamage, CriticalDamageDelta);
  }

  if (FighterNameText) {
    FighterNameText->SetText(FText::FromName(Fighter->GetFighterId()));
  }

  if (FighterImage) {
    if (UTexture2D *PortraitTexture = Fighter->GetPortraitTexture()) {
      FighterImage->SetBrushFromTexture(PortraitTexture);
      FighterImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    } else {
      FighterImage->SetBrushFromTexture(nullptr);
      FighterImage->SetVisibility(ESlateVisibility::Collapsed);
    }
  }
}

void UBattleHUDWidget::ClearPrimaryStatPanel() {
  DisplayedFriendlyStatFighter.Reset();

  const auto ClearText = [](UTextBlock *Widget) {
    if (Widget) {
      Widget->SetText(FText::GetEmpty());
    }
  };

  ClearText(HealthText);
  ClearText(AttackText);
  ClearText(CriticalDamageText);
  ClearText(MoveText);
  ClearText(ActionsText);
  ClearText(StrengthText);
  ClearText(DefenceText);
  ClearText(AttackRangeText);
  ClearText(AttackDiceText);

  ResetStatTextColor(HealthText);
  ResetStatTextColor(AttackText);
  ResetStatTextColor(CriticalDamageText);
  ResetStatTextColor(MoveText);
  ResetStatTextColor(ActionsText);
  ResetStatTextColor(StrengthText);
  ResetStatTextColor(DefenceText);
  ResetStatTextColor(AttackRangeText);
  ResetStatTextColor(AttackDiceText);

  if (FighterNameText) {
    FighterNameText->SetText(FText::GetEmpty());
  }

  if (FighterImage) {
    FighterImage->SetBrushFromTexture(nullptr);
    FighterImage->SetVisibility(ESlateVisibility::Collapsed);
  }
}

AFighterPawn *UBattleHUDWidget::ResolveFriendlyStatFighter(
    AFighterPawn *Attacker, AFighterPawn *Defender) const {
  if (IsKnownFriendlyFighter(Attacker)) {
    return Attacker;
  }
  if (IsKnownFriendlyFighter(Defender)) {
    return Defender;
  }
  return nullptr;
}

AFighterPawn *UBattleHUDWidget::ResolveEnemyStatFighter(AFighterPawn *Attacker,
                                                         AFighterPawn *Defender) const {
  if (bManualAttackRollPromptActive && ManualAttackRollTarget.IsValid()) {
    return ManualAttackRollTarget.Get();
  }

  AFighterPawn *Friendly = ResolveFriendlyStatFighter(Attacker, Defender);
  if (Friendly) {
    AFighterPawn *Candidate = (Friendly == Attacker) ? Defender : Attacker;
    if (Candidate) {
      return Candidate;
    }
  }

  if (IsKnownEnemyFighter(Attacker)) {
    return Attacker;
  }
  if (IsKnownEnemyFighter(Defender)) {
    return Defender;
  }

  if (Friendly) {
    return (Friendly == Attacker) ? Defender : Attacker;
  }

  return Attacker ? Attacker : Defender;
}

bool UBattleHUDWidget::IsFriendlyCandidate(const AFighterPawn *Fighter) const {
  if (!Fighter) {
    return false;
  }

  if (IsKnownEnemyFighter(Fighter)) {
    return false;
  }

  if (const ASkaldPlayerController *SkaldController =
          Cast<ASkaldPlayerController>(GetOwningPlayer())) {
    return SkaldController->IsFriendlyFighter(Fighter);
  }

  if (KnownFriendlyFighters.Contains(const_cast<AFighterPawn *>(Fighter))) {
    return true;
  }

  if (ActiveLockedInFighter.IsValid() &&
      ActiveLockedInFighter.Get() == Fighter) {
    return true;
  }

  if (HighlightedLockedInFighter.IsValid() &&
      HighlightedLockedInFighter.Get() == Fighter) {
    return true;
  }

  for (const TWeakObjectPtr<AFighterPawn> &Ptr : LockedInFighterOrder) {
    if (Ptr.Get() == Fighter) {
      return true;
    }
  }

  if (DisplayedFriendlyStatFighter.IsValid() &&
      DisplayedFriendlyStatFighter.Get() == Fighter) {
    return true;
  }

  return false;
}

bool UBattleHUDWidget::IsKnownFriendlyFighter(const AFighterPawn *Fighter) const {
  if (!Fighter) {
    return false;
  }

  if (BoundFighter == Fighter) {
    return bBoundFighterIsFriendly;
  }

  return IsFriendlyCandidate(Fighter);
}

bool UBattleHUDWidget::IsKnownEnemyFighter(const AFighterPawn *Fighter) const {
  if (!Fighter) {
    return false;
  }

  if (DisplayedEnemyStatFighter.IsValid() &&
      DisplayedEnemyStatFighter.Get() == Fighter) {
    return true;
  }

  if (ActiveEnemyLockedInFighter.IsValid() &&
      ActiveEnemyLockedInFighter.Get() == Fighter) {
    return true;
  }

  if (KnownEnemyFighters.Contains(const_cast<AFighterPawn *>(Fighter))) {
    return true;
  }

  for (const TWeakObjectPtr<AFighterPawn> &Ptr : EnemyLockedInFighterOrder) {
    if (Ptr.Get() == Fighter) {
      return true;
    }
  }

  return false;
}

void UBattleHUDWidget::RefreshAbilityDisplay() {
  AbilitySlotDefinitions.Empty();

  USkaldAbilityComponent *AbilityComp = BoundAbilityComponent.Get();
  if (!AbilityComp) {
    PassiveAbilityDefinition = FSkaldAbilityDefinition();
    UpdatePassiveAbilityIcon(PassiveAbilityIcon, PassiveAbilityDefinition);
    OnAbilityDisplayChanged.Broadcast(PassiveAbilityDefinition,
                                      AbilitySlotDefinitions);
    UpdateAbilityButtons();
    return;
  }

  PassiveAbilityDefinition = AbilityComp->GetPassiveAbility();
  UpdatePassiveAbilityIcon(PassiveAbilityIcon, PassiveAbilityDefinition);
  const ESkaldAbilitySlot SlotOrder[] = {ESkaldAbilitySlot::Ability1,
                                         ESkaldAbilitySlot::Ability2,
                                         ESkaldAbilitySlot::Ability3};

  for (ESkaldAbilitySlot AbilitySlot : SlotOrder) {
    if (const FSkaldAbilityState *State =
            AbilityComp->FindAbilityState(AbilitySlot)) {
      FBattleAbilitySlotDisplay Display;
      Display.Slot = AbilitySlot;
      Display.Definition = State->Definition;
      Display.CooldownRemaining = State->CooldownRemaining;
      Display.bHasBeenUsed = State->bHasBeenUsed;
      Display.bIsOnCooldown = State->bIsOnCooldown;
      Display.bCanActivate = AbilityComp->CanActivateAbility(AbilitySlot);
      AbilitySlotDefinitions.Add(Display);
    }
  }

  OnAbilityDisplayChanged.Broadcast(PassiveAbilityDefinition,
                                    AbilitySlotDefinitions);
  UpdateAbilityButtons();
}

void UBattleHUDWidget::UpdateAbilityButtons() {
  UpdateAbilityButtonForSlot(ESkaldAbilitySlot::Ability1, AbilityButton1,
                             AbilityIcon1, AbilityLabel1);
  UpdateAbilityButtonForSlot(ESkaldAbilitySlot::Ability2, AbilityButton2,
                             AbilityIcon2, AbilityLabel2);
  UpdateAbilityButtonForSlot(ESkaldAbilitySlot::Ability3, AbilityButton3,
                             AbilityIcon3, AbilityLabel3);
}

void UBattleHUDWidget::UpdateAbilityButtonForSlot(ESkaldAbilitySlot AbilitySlot,
                                                  UButton *Button,
                                                  UImage *IconWidget,
                                                  UTextBlock *LabelWidget) {
  if (!Button) {
    if (IconWidget) {
      IconWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
    if (LabelWidget) {
      LabelWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
    return;
  }

  const FBattleAbilitySlotDisplay *Display = FindAbilityDisplay(AbilitySlot);
  if (!Display) {
    Button->SetVisibility(ESlateVisibility::Collapsed);
    Button->SetIsEnabled(false);
    ApplyTooltipToWidget(Button, FText::GetEmpty());
    if (IconWidget) {
      IconWidget->SetBrushFromTexture(nullptr);
      IconWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
    if (LabelWidget) {
      LabelWidget->SetText(FText::GetEmpty());
      LabelWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
    return;
  }

  Button->SetVisibility(ESlateVisibility::Visible);
  Button->SetIsEnabled(Display->bCanActivate);

  const FSkaldAbilityDefinition &Definition = Display->Definition;

  if (IconWidget) {
    UTexture2D *IconTexture = nullptr;
    if (!Definition.AbilityIcon.IsNull()) {
      IconTexture = Definition.AbilityIcon.Get();
      if (!IconTexture) {
        IconTexture = Definition.AbilityIcon.LoadSynchronous();
      }
    }

    if (IconTexture) {
      IconWidget->SetBrushFromTexture(IconTexture);
      IconWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
    } else {
      IconWidget->SetBrushFromTexture(nullptr);
      IconWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
  }

  const FText AbilityName = !Definition.AbilityName.IsEmpty()
                               ? Definition.AbilityName
                               : Definition.AbilityDescription;

  if (LabelWidget) {
    const bool bHasName = !Definition.AbilityName.IsEmpty();
    LabelWidget->SetText(Definition.AbilityName);
    LabelWidget->SetVisibility(bHasName ? ESlateVisibility::HitTestInvisible
                                        : ESlateVisibility::Collapsed);
  }

  FText CostLabel = Definition.BuildCostLabel();
  TArray<FText> TooltipLines;
  if (!Definition.AbilityDescription.IsEmpty()) {
    TooltipLines.Add(Definition.AbilityDescription);
  }
  if (Definition.CooldownRounds > 0) {
    if (Display->bIsOnCooldown && Display->CooldownRemaining > 0) {
      TooltipLines.Add(FText::Format(
          NSLOCTEXT("SkaldBattleHUD", "AbilityTooltipCooldownRemaining",
                     "Cooldown: {0} rounds remaining"),
          FText::AsNumber(Display->CooldownRemaining)));
    } else {
      TooltipLines.Add(FText::Format(
          NSLOCTEXT("SkaldBattleHUD", "AbilityTooltipCooldown",
                     "Cooldown: {0} rounds"),
          FText::AsNumber(Definition.CooldownRounds)));
    }
  }
  if (Definition.bOncePerBattle) {
    TooltipLines.Add(NSLOCTEXT("SkaldBattleHUD", "AbilityTooltipOncePerBattle",
                               "Limited: Once per battle"));
  }

  FText BodyText = TooltipLines.Num() > 0
                       ? FText::Join(FText::FromString(TEXT("\n")), TooltipLines)
                       : FText::GetEmpty();

  const bool bHasCostLabel = !CostLabel.IsEmpty();
  const bool bHasBodyText = !BodyText.IsEmpty();
  FText TooltipText;
  if (bHasCostLabel && bHasBodyText) {
    TooltipText = FText::Format(NSLOCTEXT("SkaldBattleHUD", "AbilityTooltipFull",
                                         "{0}\nCost: {1}\n{2}"),
                                AbilityName, CostLabel, BodyText);
  } else if (bHasCostLabel) {
    TooltipText = FText::Format(NSLOCTEXT("SkaldBattleHUD",
                                          "AbilityTooltipNameCost",
                                          "{0}\nCost: {1}"),
                                AbilityName, CostLabel);
  } else if (bHasBodyText) {
    TooltipText = FText::Format(NSLOCTEXT("SkaldBattleHUD",
                                          "AbilityTooltipNameDescription",
                                          "{0}\n{1}"),
                                AbilityName, BodyText);
  } else {
    TooltipText = AbilityName;
  }

  ApplyTooltipToWidget(Button, TooltipText);
}

const FBattleAbilitySlotDisplay *
UBattleHUDWidget::FindAbilityDisplay(ESkaldAbilitySlot AbilitySlot) const {
  for (const FBattleAbilitySlotDisplay &Display : AbilitySlotDefinitions) {
    if (Display.Slot == AbilitySlot) {
      return &Display;
    }
  }
  return nullptr;
}

void UBattleHUDWidget::HandleAbilityTriggered(
    USkaldAbilityComponent *AbilityComponent,
    const FSkaldAbilityDefinition &AbilityDefinition) {
  if (!BoundAbilityComponent.IsValid() ||
      BoundAbilityComponent.Get() != AbilityComponent) {
    return;
  }

  if (!AbilityTriggeredText) {
    return;
  }

  const FText AbilityLabel = !AbilityDefinition.AbilityName.IsEmpty()
                                 ? AbilityDefinition.AbilityName
                                 : AbilityDefinition.AbilityDescription;

  if (AbilityLabel.IsEmpty()) {
    HideAbilityTriggeredText();
    return;
  }

  AbilityTriggeredText->SetText(AbilityLabel);
  AbilityTriggeredText->SetVisibility(ESlateVisibility::HitTestInvisible);

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AbilityTriggerHideTimer);
    World->GetTimerManager().SetTimer(
        AbilityTriggerHideTimer, this,
        &UBattleHUDWidget::HideAbilityTriggeredText,
        AbilityTriggerDisplayDuration, false);
  }
}

void UBattleHUDWidget::HideAbilityTriggeredText() {
  if (!AbilityTriggeredText) {
    return;
  }

  AbilityTriggeredText->SetText(FText::GetEmpty());
  AbilityTriggeredText->SetVisibility(ESlateVisibility::Collapsed);
}

void UBattleHUDWidget::ApplyTooltipToWidget(UWidget *Widget,
                                            const FText &TooltipText) const {
  USkaldTooltipStatics::ApplyTooltip(Widget, UniversalTooltipClass, TooltipText);
}

void UBattleHUDWidget::UpgradeStatIconTooltips() const {
  UpgradeWidgetTooltip(HealthImage);
  UpgradeWidgetTooltip(StrengthImage);
  UpgradeWidgetTooltip(DefenceImage);
  UpgradeWidgetTooltip(AttackDiceImage);
  UpgradeWidgetTooltip(AttackDamageImage);
  UpgradeWidgetTooltip(AttackRangeImage);
  UpgradeWidgetTooltip(MovementImage);
  UpgradeWidgetTooltip(EnemyAttackDiceImage);
  UpgradeWidgetTooltip(EnemyStrengthImage);
  UpgradeWidgetTooltip(EnemyDefenceImage);
  UpgradeWidgetTooltip(EnemyAttackDamageImage);
  UpgradeWidgetTooltip(EnemyAttackRangeImage);
  UpgradeWidgetTooltip(EnemyMovementImage);
}

void UBattleHUDWidget::UpgradeWidgetTooltip(UWidget *Widget) const {
  if (!Widget) {
    return;
  }

  USkaldTooltipStatics::UpgradeExistingTooltip(Widget, UniversalTooltipClass);
}

FSkaldAbilityDefinition
UBattleHUDWidget::ResolvePassiveAbilityDefinition(AFighterPawn *Fighter) const {
  if (!Fighter) {
    return FSkaldAbilityDefinition();
  }

  if (USkaldAbilityComponent *AbilityComponent =
          Fighter->GetAbilityComponent()) {
    return AbilityComponent->GetPassiveAbility();
  }

  return FSkaldAbilityDefinition();
}

FSkaldAbilityDefinition
UBattleHUDWidget::ResolveTieredAbilityDefinition(AFighterPawn *Fighter) const {
  if (!Fighter) {
    return FSkaldAbilityDefinition();
  }

  if (USkaldAbilityComponent *AbilityComponent =
          Fighter->GetAbilityComponent()) {
    if (const FSkaldAbilityState *TieredAbilityState =
            AbilityComponent->FindAbilityState(ESkaldAbilitySlot::Ability1)) {
      return TieredAbilityState->Definition;
    }
  }

  return FSkaldAbilityDefinition();
}

FText UBattleHUDWidget::BuildAbilityTooltipText(
    const FSkaldAbilityDefinition &Definition) const {
  return USkaldTooltipStatics::BuildBasicAbilityTooltip(Definition);
}

void UBattleHUDWidget::UpdatePassiveAbilityIcon(
    UImage *IconWidget, const FSkaldAbilityDefinition &Definition) {
  if (!IconWidget) {
    return;
  }

  if (!Definition.IsValid()) {
    IconWidget->SetBrushFromTexture(nullptr);
    IconWidget->SetVisibility(ESlateVisibility::Collapsed);
    ApplyTooltipToWidget(IconWidget, FText::GetEmpty());
    return;
  }

  UTexture2D *IconTexture = nullptr;
  if (!Definition.AbilityIcon.IsNull()) {
    IconTexture = Definition.AbilityIcon.Get();
    if (!IconTexture) {
      IconTexture = Definition.AbilityIcon.LoadSynchronous();
    }
  }

  IconWidget->SetBrushFromTexture(IconTexture);
  IconWidget->SetVisibility(ESlateVisibility::Visible);
  ApplyTooltipToWidget(IconWidget, BuildAbilityTooltipText(Definition));
}

void UBattleHUDWidget::SetRoundInfo(const FText &RoundLabel,
                                    const FText &InitiativeLabel) {
  if (RoundText) {
    RoundText->SetText(RoundLabel);
  }
  if (InitiativeText) {
    InitiativeText->SetText(InitiativeLabel);
    const bool bHasInitiativeText = !InitiativeLabel.IsEmptyOrWhitespace();
    InitiativeText->SetVisibility(bHasInitiativeText
                                      ? ESlateVisibility::HitTestInvisible
                                      : ESlateVisibility::Collapsed);

    if (UWorld *World = GetWorld()) {
      FTimerManager &TimerManager = World->GetTimerManager();
      TimerManager.ClearTimer(InitiativeHideTimer);

      if (bHasInitiativeText) {
        const TWeakObjectPtr<UBattleHUDWidget> WeakThis(this);
        FTimerDelegate TimerDelegate;
        TimerDelegate.BindLambda([WeakThis]() {
          if (WeakThis.IsValid()) {
            WeakThis->HideInitiativeText();
          }
        });

        TimerManager.SetTimer(InitiativeHideTimer, TimerDelegate, 3.f, false);
      }
    }
  }
}

void UBattleHUDWidget::SetPlayersTurnLabel(const FText &PlayerLabel) {
  if (PlayersTurnText) {
    PlayersTurnText->SetText(PlayerLabel);
  }
}

void UBattleHUDWidget::SetTerritoryName(const FText &TerritoryLabel) {
  if (TerritoryText) {
    TerritoryText->SetText(TerritoryLabel);
    TerritoryText->SetVisibility(TerritoryLabel.IsEmptyOrWhitespace()
                                     ? ESlateVisibility::Collapsed
                                     : ESlateVisibility::HitTestInvisible);
  }
}

void UBattleHUDWidget::SetSelectedFighterName(const FText &Name) {
  const bool bShouldDisplayFriendly = bBoundFighterIsFriendly && BoundFighter;

  if (FighterNameText) {
    if (bShouldDisplayFriendly) {
      FighterNameText->SetText(Name);
    } else if (!BoundFighter) {
      FighterNameText->SetText(Name);
    }
  }

  if (FighterImage) {
    if (bShouldDisplayFriendly) {
      if (UTexture2D *PortraitTexture = BoundFighter->GetPortraitTexture()) {
        FighterImage->SetBrushFromTexture(PortraitTexture);
        FighterImage->SetVisibility(ESlateVisibility::HitTestInvisible);
      } else {
        FighterImage->SetBrushFromTexture(nullptr);
        FighterImage->SetVisibility(ESlateVisibility::Collapsed);
      }
    } else if (!BoundFighter) {
      FighterImage->SetBrushFromTexture(nullptr);
      FighterImage->SetVisibility(ESlateVisibility::Collapsed);
    }
  }
}

void UBattleHUDWidget::SetActivateEnabled(bool bEnabled) {
  if (ActivateButton) {
    ActivateButton->SetIsEnabled(bEnabled);
  }
}

void UBattleHUDWidget::SetActivateVisibility(bool bVisible) {
  if (ActivateButton) {
    ActivateButton->SetVisibility(bVisible ? ESlateVisibility::Visible
                                           : ESlateVisibility::Collapsed);
  }
}

void UBattleHUDWidget::SetEndTurnEnabled(bool bEnabled) {
  if (EndTurnButton) {
    EndTurnButton->SetIsEnabled(bEnabled);
  }
}

void UBattleHUDWidget::SetEndTurnVisibility(bool bVisible) {
  bEndTurnVisibilityRequested = bVisible;
  RefreshEndTurnButtonVisibility();
}

void UBattleHUDWidget::SetActionButtonsVisibility(bool bVisible) {
  bActionButtonsUnlocked = bVisible;
  UpdateActionButtonVisibility();
}

void UBattleHUDWidget::SetAttackRollButtonVisibility(bool bVisible) {
  if (!AttackRollButton) {
    return;
  }

  const ESlateVisibility Desired =
      bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
  AttackRollButton->SetVisibility(Desired);
  AttackRollButton->SetIsEnabled(bVisible);
}

void UBattleHUDWidget::EnterManualAttackRollPrompt(AFighterPawn *Attacker) {
  if (!Attacker) {
    ManualAttackRollAttacker.Reset();
    ManualAttackRollTarget.Reset();
    bManualAttackRollPromptActive = false;
    SetAttackRollButtonVisibility(false);
    UpdateActionButtonVisibility();
    RefreshEndTurnButtonVisibility();
    return;
  }

  ManualAttackRollAttacker = Attacker;
  ManualAttackRollTarget = Attacker->GetPendingPhysicalAttackTarget();
  bManualAttackRollPromptActive = true;
  if (ManualAttackRollTarget.IsValid()) {
    UpdateEnemyStatPanel(ManualAttackRollTarget.Get());
  }
  SetAttackRollButtonVisibility(true);
  UpdateActionButtonVisibility();
  RefreshEndTurnButtonVisibility();
}

void UBattleHUDWidget::ExitManualAttackRollPrompt() {
  ManualAttackRollAttacker.Reset();
  ManualAttackRollTarget.Reset();
  if (!bManualAttackRollPromptActive) {
    RefreshEndTurnButtonVisibility();
    return;
  }

  bManualAttackRollPromptActive = false;
  SetAttackRollButtonVisibility(false);
  UpdateActionButtonVisibility();
  RefreshEndTurnButtonVisibility();
}

void UBattleHUDWidget::RefreshEndTurnButtonVisibility() {
  if (!EndTurnButton) {
    return;
  }

  const bool bShouldShow = bEndTurnVisibilityRequested && !bManualAttackRollPromptActive;
  const ESlateVisibility DesiredVisibility =
      bShouldShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
  EndTurnButton->SetVisibility(DesiredVisibility);
}

void UBattleHUDWidget::ShowDiceRoll(int32 RollValue, float DisplayDuration) {
  if (!DiceRollerImage) {
    return;
  }

  const float DiceDisplaySize = 90.f;
  const float DiceBoardPadding = 24.f;
  const FVector2D DiceOffset(0.f, 80.f);

  UObject *DiceResource = nullptr;
  const int32 Index = RollValue - 1;
  if (DiceFaceTextures.IsValidIndex(Index)) {
    DiceResource = DiceFaceTextures[Index];
  }

  if (!DiceResource) {
    if (!DiceRollerRenderTarget) {
      DiceRollerRenderTarget = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(
          this, UCanvasRenderTarget2D::StaticClass(), 256, 256);
      if (DiceRollerRenderTarget) {
        DiceRollerRenderTarget->OnCanvasRenderTargetUpdate.AddDynamic(
            this, &UBattleHUDWidget::HandleDiceRenderTargetUpdate);
      }
    }

    if (DiceRollerRenderTarget) {
      PendingDiceRenderValue = RollValue;
      DiceRollerRenderTarget->ClearColor = FLinearColor::Transparent;
      DiceRollerRenderTarget->UpdateResourceImmediate();
      DiceResource = DiceRollerRenderTarget;
    }
  }

  bool bDisplayedRoll = false;
  if (DiceResource) {
    if (UTexture2D *Texture = Cast<UTexture2D>(DiceResource)) {
      DiceRollerImage->SetBrushFromTexture(Texture, true);
    } else {
      FSlateBrush Brush = DiceRollerImage->GetBrush();
      Brush.SetResourceObject(DiceResource);
      Brush.ImageSize = FVector2D(DiceDisplaySize, DiceDisplaySize);
      DiceRollerImage->SetBrush(Brush);
    }
    DiceRollerImage->SetDesiredSizeOverride(
        FVector2D(DiceDisplaySize, DiceDisplaySize));
    DiceRollerImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

    if (UCanvasPanelSlot *DiceSlot = Cast<UCanvasPanelSlot>(DiceRollerImage->Slot)) {
      DiceSlot->SetAnchors(FAnchors(0.5f, 0.f));
      DiceSlot->SetAlignment(FVector2D(0.5f, 0.f));
      DiceSlot->SetPosition(DiceOffset);
      DiceSlot->SetSize(FVector2D(DiceDisplaySize, DiceDisplaySize));
    }

    DiceRollerImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    if (DiceBoardImage) {
      DiceBoardImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

      if (UCanvasPanelSlot *BoardSlot = Cast<UCanvasPanelSlot>(DiceBoardImage->Slot)) {
        BoardSlot->SetAnchors(FAnchors(0.5f, 0.f));
        BoardSlot->SetAlignment(FVector2D(0.5f, 0.f));
        BoardSlot->SetPosition(DiceOffset - FVector2D(0.f, DiceBoardPadding * 0.5f));
        BoardSlot->SetSize(FVector2D(DiceDisplaySize + DiceBoardPadding,
                                     DiceDisplaySize + DiceBoardPadding));
      }

      DiceBoardImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    }

    bDisplayedRoll = true;
  } else {
    DiceRollerImage->SetVisibility(ESlateVisibility::Collapsed);
    if (DiceBoardImage) {
      DiceBoardImage->SetVisibility(ESlateVisibility::Collapsed);
    }
  }

  if (bDisplayedRoll && DiceRollSound) {
    UGameplayStatics::PlaySound2D(this, DiceRollSound);
  }

  if (UWorld *World = GetWorld()) {
    FTimerManager &TimerManager = World->GetTimerManager();
    TimerManager.ClearTimer(DiceRollerHideTimer);

    const TWeakObjectPtr<UBattleHUDWidget> WeakThis(this);
    FTimerDelegate TimerDelegate;
    TimerDelegate.BindLambda([WeakThis]() {
      if (WeakThis.IsValid()) {
        WeakThis->HideDiceRoller();
      }
    });

    const float ClampedDuration = FMath::Clamp(DisplayDuration, 0.35f, 1.25f);
    TimerManager.SetTimer(DiceRollerHideTimer, TimerDelegate, ClampedDuration,
                          false);
  }
}

void UBattleHUDWidget::ClearCommandPreviews() {
  bMoveSelected = false;
  bDisengageSelected = false;
  bAttackSelected = false;
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    Grid->ClearHighlights();
  }
}

void UBattleHUDWidget::UpdateActionButtonVisibility() {
  const bool bShouldShow = bActionButtonsUnlocked && BoundFighter &&
                           bBoundFighterIsFriendly &&
                           BoundFighter->ActionsRemaining > 0 &&
                           !bManualAttackRollPromptActive;
  const bool bShowMoveButton =
      bShouldShow && BoundFighter && !BoundFighter->IsEngaged();
  const bool bShowDisengageButton =
      bShouldShow && BoundFighter && BoundFighter->IsEngaged();
  const ESlateVisibility MoveVisibility =
      bShowMoveButton ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
  const ESlateVisibility DisengageVisibility =
      bShowDisengageButton ? ESlateVisibility::Visible
                           : ESlateVisibility::Collapsed;
  const ESlateVisibility AttackVisibility =
      bShouldShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;

  if (MoveButton) {
    MoveButton->SetVisibility(MoveVisibility);
  }
  if (DisengageButton) {
    DisengageButton->SetVisibility(DisengageVisibility);
    DisengageButton->SetIsEnabled(bShowDisengageButton);
  }
  if (AttackButton) {
    AttackButton->SetVisibility(AttackVisibility);
  }
  if (!bShowMoveButton) {
    bMoveSelected = false;
  }
  if (!bShowDisengageButton) {
    bDisengageSelected = false;
  }
}

UGridOverlayComponent *UBattleHUDWidget::FindGridOverlay() const {
  if (UWorld *World = GetWorld()) {
    return Skald::GridOverlay::FindActiveGridOverlay(World);
  }

  return nullptr;
}

void UBattleHUDWidget::HideDiceRoller() {
  if (!DiceRollerImage) {
    return;
  }
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(DiceRollerHideTimer);
  }
  DiceRollerImage->SetVisibility(ESlateVisibility::Collapsed);
  DiceRollerImage->SetBrushFromTexture(nullptr);
  PendingDiceRenderValue = 0;
  if (DiceBoardImage) {
    DiceBoardImage->SetVisibility(ESlateVisibility::Collapsed);
  }
}

void UBattleHUDWidget::HandleDiceRenderTargetUpdate(UCanvas *Canvas, int32 Width,
                                                    int32 Height) {
  if (!Canvas || Width <= 0 || Height <= 0) {
    return;
  }

  const FVector2D Size(Width, Height);
  const FLinearColor BackgroundColor(0.f, 0.f, 0.f, 0.75f);
  FCanvasTileItem Tile(FVector2D::ZeroVector, Size, BackgroundColor);
  Tile.BlendMode = SE_BLEND_Translucent;
  Canvas->DrawItem(Tile);

  const FLinearColor BorderColor(1.f, 1.f, 1.f, 0.85f);
  const float BorderThickness = 6.f;
  FCanvasBoxItem Border(FVector2D::ZeroVector, Size);
  Border.SetColor(BorderColor);
  Border.LineThickness = BorderThickness;
  Canvas->DrawItem(Border);

  if (!GEngine) {
    return;
  }

  UFont *Font = GEngine->GetLargeFont();
  if (!Font) {
    Font = GEngine->GetMediumFont();
  }
  if (!Font) {
    Font = GEngine->GetSmallFont();
  }

  if (!Font || PendingDiceRenderValue <= 0) {
    return;
  }

  const FString RollString = FString::FromInt(PendingDiceRenderValue);
  FCanvasTextItem TextItem(FVector2D::ZeroVector, FText::FromString(RollString),
                           Font, FLinearColor::White);
  TextItem.bCentreX = true;
  TextItem.bCentreY = true;
  TextItem.EnableShadow(FLinearColor::Black);
  TextItem.Scale = FVector2D(2.6f, 2.6f);

  const FVector2D Center(Width * 0.5f, Height * 0.5f);
  Canvas->DrawItem(TextItem, Center);
}

void UBattleHUDWidget::HideInitiativeText() {
  if (!InitiativeText) {
    return;
  }
  InitiativeText->SetVisibility(ESlateVisibility::Collapsed);
}

void UBattleHUDWidget::ShowCombatFloater(const FVector &WorldLocation,
                                         const FText &Message,
                                         const FLinearColor &Tint, float Scale,
                                         bool bUseMissStyling,
                                         float LifetimeOverride) {
  UCombatFloaterPoolSubsystem *Pool = ResolveFloaterPool();
  if (!Pool) {
    return;
  }

  if (FloaterWidgetClass) {
    Pool->FloaterWidgetClass = FloaterWidgetClass;
  }

  APlayerController *OwningController = GetOwningPlayer();
  if (!OwningController) {
    return;
  }

  if (UW_FloatingText *Floater = Pool->SpawnFloater(OwningController)) {
    Floater->SetText(Message);
    Floater->SetColorAndOpacity(Tint);
    Floater->SetFloaterOpacity(1.f);
    Floater->SetTagStyle(bUseMissStyling);
    Floater->SetFloaterScale(Scale);

    FBattleActiveFloater &Entry = ActiveFloaters.AddDefaulted_GetRef();
    Entry.Floater = Floater;
    Entry.AnchorLocation = WorldLocation;
    Entry.InitialOffset = FVector2D(FMath::RandRange(-18.f, 18.f), 0.f);
    Entry.HorizontalDirection = FMath::RandBool() ? 1.f : -1.f;
    Entry.Lifetime = LifetimeOverride > 0.f ? LifetimeOverride : FloaterLifetime;
    Entry.Lifetime = FMath::Max(Entry.Lifetime, 0.1f);
    Entry.FadeDuration = FloaterFadeDuration;
    Entry.Elapsed = 0.f;
    Entry.Scale = Scale;

    Floater->UpdateProjection(WorldLocation, Entry.InitialOffset,
                              FloaterClampMargin);
  }
}

void UBattleHUDWidget::ShowMissTag(AFighterPawn *Target) {
  if (!Target) {
    return;
  }

  const float CapsuleHalfHeight = Target->GetSimpleCollisionHalfHeight();
  const float AnchorHeight = CapsuleHalfHeight > KINDA_SMALL_NUMBER
                                 ? CapsuleHalfHeight
                                 : 88.f;
  const FVector AnchorLocation =
      Target->GetActorLocation() +
      FVector(0.f, 0.f, AnchorHeight + FloaterAnchorHeightOffset);

  const int32 NewEntryIndex = ActiveFloaters.Num();
  const FText MissLabel =
      NSLOCTEXT("SkaldBattle", "DiceMissTag", "MISS");
  ShowCombatFloater(AnchorLocation, MissLabel, MissFloaterColor, 0.9f, true,
                    0.8f);

  if (!ActiveFloaters.IsValidIndex(NewEntryIndex)) {
    return;
  }

  FBattleActiveFloater &Entry = ActiveFloaters[NewEntryIndex];
  const float DriftDenominator = FMath::Max(FloaterHorizontalDrift, 1.f);
  const float DriftMagnitude = FMath::RandRange(10.f, 16.f) / DriftDenominator;
  const float DriftDirection = FMath::RandBool() ? 1.f : -1.f;
  Entry.HorizontalDirection = DriftDirection * DriftMagnitude;
  Entry.InitialOffset = FVector2D(FMath::RandRange(-6.f, 6.f),
                                  FMath::RandRange(-4.f, 4.f));
  Entry.Scale = 0.9f;
  Entry.FadeDuration = FMath::Min(Entry.FadeDuration, 0.28f);

  if (UW_FloatingText *Floater = Entry.Floater.Get()) {
    Floater->UpdateProjection(Entry.AnchorLocation, Entry.InitialOffset,
                              FloaterClampMargin);
  }
}

void UBattleHUDWidget::ShowAttackResultFloater(AFighterPawn *Target,
                                               const FDiceRollResult &Result) {
  if (!Target) {
    UE_LOG(LogSkald, Verbose,
           TEXT("ShowAttackResultFloater skipped (no target). Damage=%d Hits=%d Crits=%d Misses=%d HP=%d->%d"),
           Result.TotalDamage, Result.HitCount, Result.CriticalHitCount,
           Result.MissCount, Result.StartingHealth, Result.EndingHealth);
    return;
  }

  const float CapsuleHalfHeight = Target->GetSimpleCollisionHalfHeight();
  const float AnchorHeight = CapsuleHalfHeight > KINDA_SMALL_NUMBER
                                 ? CapsuleHalfHeight
                                 : 88.f;
  const FVector BaseLocation =
      Target->GetActorLocation() +
      FVector(0.f, 0.f, AnchorHeight + FloaterAnchorHeightOffset);

  int32 FloaterIndex = 0;
  const auto SpawnFloater = [&](const FText &Label, const FLinearColor &Tint,
                                float Scale, bool bMissStyle,
                                float LifetimeOverride = -1.f) {
    const FVector StackLocation =
        BaseLocation + FVector(0.f, 0.f, FloaterStackSpacing * FloaterIndex);
    ShowCombatFloater(StackLocation, Label, Tint, Scale, bMissStyle,
                      LifetimeOverride);
    ++FloaterIndex;
  };

  const int32 Damage = FMath::Max(Result.TotalDamage, 0);
  const bool bAnyHits = Result.HitCount > 0 || Damage > 0;
  const bool bAnyCrits = Result.CriticalHitCount > 0;

  const FSkaldFloaterStyle *AppliedCritStyle = nullptr;
  if (bAnyCrits)
  {
    if (Result.bHighStakesCritical)
    {
      const FSkaldFloaterStyle *FactionOverride = Result.HighStakesFaction != ESkaldFaction::None
                                                      ? HighStakesFloaterOverrides.Find(Result.HighStakesFaction)
                                                      : nullptr;
      AppliedCritStyle = FactionOverride ? FactionOverride : &HighStakesCriticalFloaterStyle;
    }
    else
    {
      AppliedCritStyle = &CriticalFloaterStyle;
    }
  }

  if (bAnyHits) {
    const FLinearColor DamageTint =
        AppliedCritStyle ? AppliedCritStyle->Color : (bAnyCrits ? CriticalFloaterStyle.Color : HitFloaterColor);
    const float DamageScale = AppliedCritStyle ? AppliedCritStyle->Scale : (bAnyCrits ? CriticalFloaterStyle.Scale : 1.1f);
    const FText DamageLabel = Damage > 0
                                  ? FText::Format(NSLOCTEXT("SkaldBattle",
                                                             "DamageFloaterLabel",
                                                             "-{0}"),
                                                  FText::AsNumber(Damage))
                                  : NSLOCTEXT("SkaldBattle",
                                              "DamageFloaterZeroLabel", "0");
    SpawnFloater(DamageLabel, DamageTint, DamageScale, false);

    if (Result.CriticalHitCount > 0) {
      const FText CritLabel = Result.CriticalHitCount > 1
                                  ? FText::Format(NSLOCTEXT("SkaldBattle",
                                                             "CritFloaterPlural",
                                                             "CRIT ×{0}"),
                                                  FText::AsNumber(
                                                      Result.CriticalHitCount))
                                  : NSLOCTEXT("SkaldBattle",
                                              "CritFloaterSingular", "CRIT!");
      const FLinearColor CritTint = AppliedCritStyle ? AppliedCritStyle->Color : CriticalFloaterStyle.Color;
      const float CritScale = AppliedCritStyle ? AppliedCritStyle->Scale : CriticalFloaterStyle.Scale;
      SpawnFloater(CritLabel, CritTint, CritScale, false);
    } else if (Result.HitCount > 1) {
      const FText HitLabel = FText::Format(
          NSLOCTEXT("SkaldBattle", "HitFloaterPlural", "Hits ×{0}"),
          FText::AsNumber(Result.HitCount));
      SpawnFloater(HitLabel, HitFloaterColor, 0.95f, false);
    }
  } else {
    const int32 MissCount = FMath::Max(Result.MissCount, 1);
    const FText MissLabel = MissCount > 1
                                ? FText::Format(NSLOCTEXT("SkaldBattle",
                                                           "MissFloaterPlural",
                                                           "Miss ×{0}"),
                                                FText::AsNumber(MissCount))
                                : NSLOCTEXT("SkaldBattle",
                                            "MissFloaterSingular", "Miss");
    SpawnFloater(MissLabel, MissFloaterColor, 1.0f, true);
  }

  const int32 StartingHealth = FMath::Max(Result.StartingHealth, 0);
  const int32 EndingHealth = FMath::Max(Result.EndingHealth, 0);
  const FText HealthLabel = FText::Format(
      NSLOCTEXT("SkaldBattle", "HealthFloaterLabel", "HP {0} → {1}"),
      FText::AsNumber(StartingHealth), FText::AsNumber(EndingHealth));
  SpawnFloater(HealthLabel, HealthFloaterColor, 0.95f, false, 2.1f);

  UE_LOG(LogSkald, Verbose,
         TEXT("ShowAttackResultFloater: Target=%s Damage=%d Hits=%d Crits=%d Misses=%d HP=%d->%d"),
         *Target->GetName(), Damage, Result.HitCount, Result.CriticalHitCount,
         Result.MissCount, Result.StartingHealth, Result.EndingHealth);
}

void UBattleHUDWidget::QueueDiceResolution(
    AFighterPawn* Attacker,
    AFighterPawn* Defender,
    const FDiceRollResult& Result,
    bool bManualReveal)
{
    FBattleQueuedDiceResolution Entry;
    Entry.Attacker = Attacker;
    Entry.Defender = Defender;
    Entry.Result = Result;

    // Manual reveal has been retired so the full resolution plays automatically
    // once the dice roll completes. Retain the parameter to avoid widespread
    // signature churn but clamp behaviour to the fully-automatic flow.
    const bool bEnableManualReveal = false;
    const bool bShouldUseManualReveal = bEnableManualReveal && bManualReveal && Result.DiceOutcomes.Num() > 0;
    Entry.bManualReveal = bShouldUseManualReveal;
    Entry.PendingManualReveals = bShouldUseManualReveal ? Result.DiceOutcomes.Num() : 0;

    PendingDiceResolutions.Add(MoveTemp(Entry));

    // Hold the health text while this resolution is animating.
    BeginHealthTextHold(Defender, Result.StartingHealth, Result.EndingHealth);

    // Start processing immediately if nothing is currently active.
    if (!bDiceResolutionActive)
    {
        ProcessNextDiceResolution();
    }
}

void UBattleHUDWidget::ProcessNextDiceResolution() {
  if (bDiceResolutionActive) {
    return;
  }

  if (PendingDiceResolutions.Num() == 0) {
    return;
  }

  bDiceResolutionActive = true;
  ActiveDiceResolution = PendingDiceResolutions[0];
  PendingDiceResolutions.RemoveAt(0);

  bManualDiceResolutionActive = ActiveDiceResolution.bManualReveal;
  if (ActiveDiceResolution.bManualReveal) {
    ActiveDiceResolution.PendingManualReveals =
        ActiveDiceResolution.Result.DiceOutcomes.Num();
  }

  if (AFighterPawn *FriendlyForDisplay =
          ResolveFriendlyStatFighter(ActiveDiceResolution.Attacker.Get(),
                                     ActiveDiceResolution.Defender.Get())) {
    ApplyPrimaryFighterDisplay(FriendlyForDisplay);
  }

  AFighterPawn *EnemyForDisplay =
      ResolveEnemyStatFighter(ActiveDiceResolution.Attacker.Get(),
                              ActiveDiceResolution.Defender.Get());
  UpdateEnemyStatPanel(EnemyForDisplay);

  if (!DiceResolutionPanel) {
    OnResolutionComplete.Broadcast(ActiveDiceResolution.Attacker.Get(),
                                   ActiveDiceResolution.Defender.Get(),
                                   ActiveDiceResolution.Result);
    ReleaseHealthTextHold(ActiveDiceResolution.Defender.Get());
    bDiceResolutionActive = false;
    bManualDiceResolutionActive = false;
    UpdateStatPanel();
    RefreshEnemyDisplayAfterResolution();
    ProcessNextDiceResolution();
    return;
  }

  const FDiceResolutionPanelLayout Layout = ResolveDiceResolutionPanelLayout(
      ActiveDiceResolution.Attacker.Get(), ActiveDiceResolution.Defender.Get(),
      ActiveDiceResolution.Result);
  ApplyDiceResolutionPanelLayoutInternal(Layout);

  DiceResolutionPanel->SetManualRevealEnabled(bManualDiceResolutionActive);

  DiceResolutionPanel->BeginResolution(ActiveDiceResolution.Result);

  if (bManualDiceResolutionActive &&
      ActiveDiceResolution.PendingManualReveals > 0) {
    SetAttackRollButtonVisibility(true);
  } else {
    SetAttackRollButtonVisibility(false);
  }
}

void UBattleHUDWidget::HandleDicePanelResolved(
    const FDiceRollResult &Result) {
  HideDiceRoller();

  if (!bDiceResolutionActive) {
    SetAttackRollButtonVisibility(false);
    ReleaseHealthTextHold(nullptr);
    OnResolutionComplete.Broadcast(nullptr, nullptr, Result);
    UpdateStatPanel();
    RefreshEnemyDisplayAfterResolution();
    return;
  }

  OnResolutionComplete.Broadcast(ActiveDiceResolution.Attacker.Get(),
                                 ActiveDiceResolution.Defender.Get(),
                                 ActiveDiceResolution.Result);

  ReleaseHealthTextHold(ActiveDiceResolution.Defender.Get());

  bDiceResolutionActive = false;
  ActiveDiceResolution = FBattleQueuedDiceResolution();
  bManualDiceResolutionActive = false;
  SetAttackRollButtonVisibility(false);
  UpdateStatPanel();
  RefreshEnemyDisplayAfterResolution();

  ProcessNextDiceResolution();
}

void UBattleHUDWidget::HandleDiceOutcomeRevealed(
    const FDiceRollOutcome &Outcome, int32 RevealIndex) {
  if (!bDiceResolutionActive) {
    OnDiceOutcomeRevealed.Broadcast(nullptr, nullptr, Outcome, RevealIndex);
    return;
  }

  if (!Outcome.bHit) {
    if (AFighterPawn *Defender = ActiveDiceResolution.Defender.Get()) {
      ShowMissTag(Defender);
    }
  }

  OnDiceOutcomeRevealed.Broadcast(ActiveDiceResolution.Attacker.Get(),
                                  ActiveDiceResolution.Defender.Get(), Outcome,
                                  RevealIndex);

  if (ActiveDiceResolution.bManualReveal &&
      ActiveDiceResolution.PendingManualReveals > 0) {
    --ActiveDiceResolution.PendingManualReveals;
    if (ActiveDiceResolution.PendingManualReveals > 0) {
      SetAttackRollButtonVisibility(true);
    }
  }
}

bool UBattleHUDWidget::IsCombatPresentationActive() const {
  return bDiceResolutionActive || ActiveFloaters.Num() > 0;
}

void UBattleHUDWidget::BeginHealthTextHold(AFighterPawn *Fighter,
                                           int32 DisplayValue,
                                           int32 FinalValue) {
  if (!Fighter) {
    return;
  }

  if (BoundFighter != Fighter) {
    if (bHealthTextHoldActive && HeldHealthTextFighter.IsValid() &&
        HeldHealthTextFighter.Get() == Fighter) {
      bHasPendingHealthTextValue = true;
      PendingHealthTextValue = FMath::Max(0, FinalValue);
    }
    return;
  }

  HeldHealthTextFighter = Fighter;
  bHealthTextHoldActive = true;
  bHasPendingHealthTextValue = true;
  PendingHealthTextValue = FMath::Max(0, FinalValue);

  if (HealthText) {
    HealthText->SetText(
        FText::AsNumber(FMath::Max(0, DisplayValue)));
  }
}

void UBattleHUDWidget::ReleaseHealthTextHold(AFighterPawn *Fighter) {
  if (!bHealthTextHoldActive) {
    return;
  }

  if (HeldHealthTextFighter.IsValid() && Fighter &&
      HeldHealthTextFighter.Get() != Fighter) {
    return;
  }

  AFighterPawn *ResolvedFighter = Fighter;
  if (!ResolvedFighter) {
    ResolvedFighter = HeldHealthTextFighter.Get();
  }
  if (!ResolvedFighter) {
    ResolvedFighter = BoundFighter;
  }

  if (ResolvedFighter && HealthText && BoundFighter == ResolvedFighter) {
    const int32 FinalValue = bHasPendingHealthTextValue
                                 ? PendingHealthTextValue
                                 : FMath::Max(0, ResolvedFighter->Stats.Health);
    HealthText->SetText(
        FText::AsNumber(FMath::Max(0, FinalValue)));
  }

  ClearHealthTextHold();
}

void UBattleHUDWidget::ClearHealthTextHold() {
  bHealthTextHoldActive = false;
  bHasPendingHealthTextValue = false;
  PendingHealthTextValue = 0;
  HeldHealthTextFighter.Reset();
}

void UBattleHUDWidget::UpdateCombatFloaters(float DeltaSeconds) {
  if (ActiveFloaters.Num() == 0) {
    return;
  }

  for (int32 Index = ActiveFloaters.Num() - 1; Index >= 0; --Index) {
    FBattleActiveFloater &Entry = ActiveFloaters[Index];
    UW_FloatingText *Floater = Entry.Floater.Get();
    if (!Floater) {
      ActiveFloaters.RemoveAtSwap(Index);
      continue;
    }

    Entry.Elapsed += DeltaSeconds;
    const float Lifetime = FMath::Max(Entry.Lifetime, 0.1f);
    const float Normalised = FMath::Clamp(Entry.Elapsed / Lifetime, 0.f, 1.f);

    const float VerticalArc = FMath::Sin(Normalised * PI) * FloaterArcHeight;
    const float Horizontal = Entry.HorizontalDirection * FloaterHorizontalDrift *
                             Normalised;
    const FVector2D Offset = Entry.InitialOffset +
                             FVector2D(Horizontal, -VerticalArc);

    Floater->UpdateProjection(Entry.AnchorLocation, Offset, FloaterClampMargin);

    const float FadeStart = FMath::Max(Lifetime - Entry.FadeDuration, 0.f);
    float Opacity = 1.f;
    if (Entry.Elapsed >= FadeStart && Entry.FadeDuration > KINDA_SMALL_NUMBER) {
      Opacity = FMath::Clamp((Lifetime - Entry.Elapsed) / Entry.FadeDuration,
                             0.f, 1.f);
    }
    Floater->SetFloaterOpacity(Opacity);
    Floater->SetFloaterScale(Entry.Scale);

    if (Entry.Elapsed >= Lifetime) {
      ReleaseFloaterAtIndex(Index);
    }
  }
}

void UBattleHUDWidget::ReleaseFloaterAtIndex(int32 Index) {
  if (!ActiveFloaters.IsValidIndex(Index)) {
    return;
  }

  if (UW_FloatingText *Floater = ActiveFloaters[Index].Floater.Get()) {
    if (UCombatFloaterPoolSubsystem *Pool = ResolveFloaterPool()) {
      Pool->ReleaseFloater(Floater);
    } else {
      Floater->RemoveFromParent();
    }
  }

  ActiveFloaters.RemoveAtSwap(Index);
}

UCombatFloaterPoolSubsystem *UBattleHUDWidget::ResolveFloaterPool() {
  if (CachedFloaterPool.IsValid()) {
    return CachedFloaterPool.Get();
  }

  if (UWorld *World = GetWorld()) {
    if (UCombatFloaterPoolSubsystem *Pool =
            World->GetSubsystem<UCombatFloaterPoolSubsystem>()) {
      CachedFloaterPool = Pool;
      return Pool;
    }
  }

  return nullptr;
}

void UBattleHUDWidget::CacheDefaultTextColor(UTextBlock *Widget) {
  if (!Widget) {
    return;
  }

  if (!CachedDefaultTextColors.Contains(Widget)) {
    CachedDefaultTextColors.Add(Widget, Widget->GetColorAndOpacity());
  }
}

void UBattleHUDWidget::ResetStatTextColor(UTextBlock *Widget) {
  if (!Widget) {
    return;
  }

  if (const FSlateColor *DefaultColor = CachedDefaultTextColors.Find(Widget)) {
    Widget->SetColorAndOpacity(*DefaultColor);
  }
}

void UBattleHUDWidget::ApplyStatText(UTextBlock *Widget, int32 Value,
                                     int32 NetDelta) {
  if (!Widget) {
    return;
  }

  CacheDefaultTextColor(Widget);
  Widget->SetText(FText::AsNumber(Value));

  if (NetDelta > 0) {
    Widget->SetColorAndOpacity(FSlateColor(BuffStatTextColor));
  } else if (NetDelta < 0) {
    Widget->SetColorAndOpacity(FSlateColor(DebuffStatTextColor));
  } else {
    ResetStatTextColor(Widget);
  }
}

void UBattleHUDWidget::ApplyStatTextWithVisibility(UTextBlock *Widget,
                                                   int32 Value,
                                                   int32 NetDelta) {
  if (!Widget) {
    return;
  }

  Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
  ApplyStatText(Widget, Value, NetDelta);
}

void UBattleHUDWidget::SetDefaultDiceResolutionPanelLayout(
    const FDiceResolutionPanelLayout &Layout) {
  DefaultDiceResolutionPanelLayout = Layout;
  ApplyDiceResolutionPanelLayoutInternal(DefaultDiceResolutionPanelLayout);
}

void UBattleHUDWidget::ApplyDiceResolutionPanelLayout(
    const FDiceResolutionPanelLayout &Layout) {
  ApplyDiceResolutionPanelLayoutInternal(Layout);
}

FDiceResolutionPanelLayout
UBattleHUDWidget::ResolveDiceResolutionPanelLayout_Implementation(
    AFighterPawn *Attacker, AFighterPawn *Defender,
    const FDiceRollResult &Result) const {
  return DefaultDiceResolutionPanelLayout;
}

void UBattleHUDWidget::ApplyDiceResolutionPanelLayoutInternal(
    const FDiceResolutionPanelLayout &Layout) {
  if (!DiceResolutionPanel || !Layout.bApplyLayout) {
    return;
  }

  if (UPanelSlot *PanelSlot = DiceResolutionPanel->Slot) {
    if (UCanvasPanelSlot *CanvasSlot = Cast<UCanvasPanelSlot>(PanelSlot)) {
      if (Layout.bOverrideAnchors) {
        CanvasSlot->SetAnchors(Layout.Anchors);
      }

      if (Layout.bOverrideAlignment) {
        CanvasSlot->SetAlignment(Layout.Alignment);
      }

      if (Layout.bOverridePosition) {
        CanvasSlot->SetPosition(Layout.Position);
      }

      if (Layout.bOverrideSize) {
        CanvasSlot->SetSize(Layout.Size);
      }
    }
  }
}
