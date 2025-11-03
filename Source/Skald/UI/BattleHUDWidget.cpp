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
#include "Styling/SlateBrush.h"
#include "Engine/World.h"
#include "FighterPawn.h"
#include "GridOverlayComponent.h"
#include "UI/CombatFloaterPoolSubsystem.h"
#include "UI/LockedInFighterEntryWidget.h"
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

  if (AbilityLabel1) {
    AbilityLabel1->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (AbilityLabel2) {
    AbilityLabel2->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (AbilityLabel3) {
    AbilityLabel3->SetVisibility(ESlateVisibility::Collapsed);
  }

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

  PendingDiceResolutions.Reset();
  bDiceResolutionActive = false;
  ActiveDiceResolution = FBattleQueuedDiceResolution();

  if (DiceRollerRenderTarget) {
    DiceRollerRenderTarget->OnCanvasRenderTargetUpdate.RemoveDynamic(
        this, &UBattleHUDWidget::HandleDiceRenderTargetUpdate);
    DiceRollerRenderTarget = nullptr;
  }

  if (AttackRollButton) {
    AttackRollButton->OnClicked.RemoveDynamic(
        this, &UBattleHUDWidget::HandleAttackRollPressed);
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

  while (ActiveFloaters.Num() > 0) {
    ReleaseFloaterAtIndex(ActiveFloaters.Num() - 1);
  }
  CachedFloaterPool.Reset();

  ClearHealthTextHold();

  if (BoundAbilityComponent.IsValid()) {
    if (USkaldAbilityComponent *Existing = BoundAbilityComponent.Get()) {
      Existing->OnAbilityStateChanged.RemoveDynamic(
          this, &UBattleHUDWidget::HandleAbilityComponentUpdated);
    }
    BoundAbilityComponent = nullptr;
  }

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
  ScrollBox_LockedInFightersList->ClearChildren();

  TSet<AFighterPawn *> DesiredSet;
  for (AFighterPawn *Fighter : Fighters) {
    if (!Fighter) {
      continue;
    }

    DesiredSet.Add(Fighter);
    LockedInFighterOrder.Add(Fighter);

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

void UBattleHUDWidget::HandleSelectedFighterChanged(AFighterPawn *Fighter) {
  SetHighlightedLockedInFighter(Fighter);
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

void UBattleHUDWidget::RefreshLockedInFighterTurnStates() {
  PruneInvalidLockedInEntries();
  for (auto &Pair : LockedInFighterEntries) {
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
      It.RemoveCurrent();
    }
  }

  LockedInFighterOrder.RemoveAll([](const TWeakObjectPtr<AFighterPawn> &Ptr) {
    return !Ptr.IsValid();
  });
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

  if (ULockedInFighterEntryWidget *NewEntry =
          CreateWidget<ULockedInFighterEntryWidget>(this,
                                                   LockedInFighterEntryClass)) {
    NewEntry->OnEntryClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleLockedInEntryClicked);
    NewEntry->OnEntryRemoved.AddDynamic(
        this, &UBattleHUDWidget::HandleLockedInEntryRemoved);
    LockedInFighterEntries.Add(Fighter, NewEntry);
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
    BoundFighter->OnHealthChanged.RemoveDynamic(
        this, &UBattleHUDWidget::HandleHealthChanged);
    BoundFighter->OnActionsChanged.RemoveDynamic(
        this, &UBattleHUDWidget::HandleActionsChanged);
  }

  if (BoundAbilityComponent.IsValid()) {
    if (USkaldAbilityComponent *Existing = BoundAbilityComponent.Get()) {
      Existing->OnAbilityStateChanged.RemoveDynamic(
          this, &UBattleHUDWidget::HandleAbilityComponentUpdated);
    }
    BoundAbilityComponent = nullptr;
  }

  if (BoundFighter && BoundFighter != Fighter) {
    ClearHealthTextHold();
  }

  BoundFighter = Fighter;
  if (BoundFighter) {
    BoundFighter->OnHealthChanged.AddDynamic(
        this, &UBattleHUDWidget::HandleHealthChanged);
    BoundFighter->OnActionsChanged.AddDynamic(
        this, &UBattleHUDWidget::HandleActionsChanged);

    if (USkaldAbilityComponent *AbilityComp =
            BoundFighter->GetAbilityComponent()) {
      BoundAbilityComponent = AbilityComp;
      AbilityComp->OnAbilityStateChanged.AddDynamic(
          this, &UBattleHUDWidget::HandleAbilityComponentUpdated);
    }

    UpdateStatPanel();
    if (FighterNameText) {
      FighterNameText->SetText(FText::FromName(BoundFighter->GetFighterId()));
    }
    if (FighterImage) {
      if (UTexture2D *PortraitTexture = BoundFighter->GetPortraitTexture()) {
        FighterImage->SetBrushFromTexture(PortraitTexture);
        FighterImage->SetVisibility(ESlateVisibility::HitTestInvisible);
      } else {
        FighterImage->SetBrushFromTexture(nullptr);
        FighterImage->SetVisibility(ESlateVisibility::Collapsed);
      }
    }
  } else {
    if (HealthText) {
      HealthText->SetText(FText::GetEmpty());
    }
    if (AttackText) {
      AttackText->SetText(FText::GetEmpty());
    }
    if (CriticalDamageText) {
      CriticalDamageText->SetText(FText::GetEmpty());
    }
    if (MoveText) {
      MoveText->SetText(FText::GetEmpty());
    }
    if (ActionsText) {
      ActionsText->SetText(FText::GetEmpty());
    }
    if (StrengthText) {
      StrengthText->SetText(FText::GetEmpty());
    }
    if (DefenceText) {
      DefenceText->SetText(FText::GetEmpty());
    }
    if (AttackRangeText) {
      AttackRangeText->SetText(FText::GetEmpty());
    }
    if (AttackDiceText) {
      AttackDiceText->SetText(FText::GetEmpty());
    }
    if (FighterNameText) {
      FighterNameText->SetText(FText::GetEmpty());
    }
    if (FighterImage) {
      FighterImage->SetBrushFromTexture(nullptr);
      FighterImage->SetVisibility(ESlateVisibility::Collapsed);
    }
  }

  RefreshAbilityDisplay();
  UpdateActionButtonVisibility();
}

void UBattleHUDWidget::HandleMovePressed() {
  OnMovePressed.Broadcast();
  bAttackSelected = false;
  if (!BoundFighter) {
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

void UBattleHUDWidget::HandleAttackPressed() {
  OnAttackPressed.Broadcast();
  bMoveSelected = false;
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
  if (!bManualDiceResolutionActive) {
    return;
  }

  OnAttackRollRequested.Broadcast();

  SetAttackRollButtonVisibility(false);

  if (!DiceResolutionPanel) {
    return;
  }

  const bool bAdvanced = DiceResolutionPanel->AdvanceManualReveal();

  if (!bAdvanced && bManualDiceResolutionActive) {
    SetAttackRollButtonVisibility(true);
  }
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
  if (bHealthTextHoldActive && HeldHealthTextFighter.IsValid() &&
      HeldHealthTextFighter.Get() == BoundFighter) {
    bHasPendingHealthTextValue = true;
    PendingHealthTextValue = FMath::Max(0, NewHealth);
    return;
  }

  if (HealthText) {
    HealthText->SetText(FText::AsNumber(NewHealth));
  }
}

void UBattleHUDWidget::HandleActionsChanged(int32 NewActions) {
  if (ActionsText) {
    ActionsText->SetText(FText::AsNumber(NewActions));
  }

  UpdateActionButtonVisibility();
  RefreshAbilityDisplay();
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
  if (!BoundFighter) {
    return;
  }
  if (HealthText) {
    HealthText->SetText(FText::AsNumber(BoundFighter->Stats.Health));
  }
  if (AttackText) {
    AttackText->SetText(FText::AsNumber(BoundFighter->Stats.AttackDamage));
  }
  if (CriticalDamageText) {
    const int32 CriticalDamage =
        BoundFighter->Stats.AttackDamage +
        BoundFighter->Stats.CriticalBonusDamage;
    CriticalDamageText->SetText(FText::AsNumber(CriticalDamage));
  }
  if (MoveText) {
    MoveText->SetText(FText::AsNumber(BoundFighter->Stats.Movement));
  }
  if (ActionsText) {
    ActionsText->SetText(FText::AsNumber(BoundFighter->ActionsRemaining));
  }
  if (StrengthText) {
    StrengthText->SetText(FText::AsNumber(BoundFighter->Stats.Strength));
  }
  if (DefenceText) {
    DefenceText->SetText(FText::AsNumber(BoundFighter->Stats.Defence));
  }
  if (AttackRangeText) {
    AttackRangeText->SetText(FText::AsNumber(BoundFighter->Stats.AttackRange));
  }
  if (AttackDiceText) {
    AttackDiceText->SetText(FText::AsNumber(BoundFighter->Stats.AttackDice));
  }

  UpdateActionButtonVisibility();
}

void UBattleHUDWidget::RefreshAbilityDisplay() {
  AbilitySlotDefinitions.Empty();

  USkaldAbilityComponent *AbilityComp = BoundAbilityComponent.Get();
  if (!AbilityComp) {
    PassiveAbilityDefinition = FSkaldAbilityDefinition();
    OnAbilityDisplayChanged.Broadcast(PassiveAbilityDefinition,
                                      AbilitySlotDefinitions);
    UpdateAbilityButtons();
    return;
  }

  PassiveAbilityDefinition = AbilityComp->GetPassiveAbility();
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
    Button->SetToolTipText(FText::GetEmpty());
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

  Button->SetToolTipText(TooltipText);
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
  if (FighterNameText) {
    FighterNameText->SetText(Name);
  }
  if (FighterImage) {
    UTexture2D *PortraitTexture =
        BoundFighter ? BoundFighter->GetPortraitTexture() : nullptr;
    if (PortraitTexture) {
      FighterImage->SetBrushFromTexture(PortraitTexture);
      FighterImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    } else {
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
  if (EndTurnButton) {
    EndTurnButton->SetVisibility(
        bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
  }
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
  bAttackSelected = false;
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    Grid->ClearHighlights();
  }
}

void UBattleHUDWidget::UpdateActionButtonVisibility() {
  const bool bShouldShow = bActionButtonsUnlocked && BoundFighter &&
                           BoundFighter->ActionsRemaining > 0;
  const ESlateVisibility DesiredVisibility =
      bShouldShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;

  if (MoveButton) {
    MoveButton->SetVisibility(DesiredVisibility);
  }
  if (AttackButton) {
    AttackButton->SetVisibility(DesiredVisibility);
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

void UBattleHUDWidget::QueueDiceResolution(AFighterPawn *Attacker,
                                           AFighterPawn *Defender,
                                           const FDiceRollResult &Result,
                                           bool bManualReveal) {
  FBattleQueuedDiceResolution Entry;
  Entry.Attacker = Attacker;
  Entry.Defender = Defender;
  Entry.Result = Result;
  Entry.bManualReveal = bManualReveal && Result.DiceOutcomes.Num() > 0;
  Entry.PendingManualReveals = Entry.bManualReveal
                                    ? Result.DiceOutcomes.Num()
                                    : 0;
  PendingDiceResolutions.Add(MoveTemp(Entry));

  if (Entry.bManualReveal) {
    SetAttackRollButtonVisibility(false);
  } else {
    SetAttackRollButtonVisibility(false);
  }

  BeginHealthTextHold(Defender, Result.StartingHealth, Result.EndingHealth);

  if (!bDiceResolutionActive) {
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

  if (!DiceResolutionPanel) {
    OnResolutionComplete.Broadcast(ActiveDiceResolution.Attacker.Get(),
                                   ActiveDiceResolution.Defender.Get(),
                                   ActiveDiceResolution.Result);
    ReleaseHealthTextHold(ActiveDiceResolution.Defender.Get());
    bDiceResolutionActive = false;
    bManualDiceResolutionActive = false;
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
