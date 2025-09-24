#include "UI/BattleHUDWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FighterPawn.h"
#include "GridOverlayComponent.h"

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
  if (ActivateButton) {
    ActivateButton->OnClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleActivatePressed);
  }
  if (EndTurnButton) {
    EndTurnButton->OnClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleEndTurnPressed);
  }
}

void UBattleHUDWidget::RefreshStats() { UpdateStatPanel(); }

void UBattleHUDWidget::BindToFighter(AFighterPawn *Fighter) {
  if (BoundFighter) {
    BoundFighter->OnHealthChanged.RemoveDynamic(
        this, &UBattleHUDWidget::HandleHealthChanged);
  }
  BoundFighter = Fighter;
  if (BoundFighter) {
    BoundFighter->OnHealthChanged.AddDynamic(
        this, &UBattleHUDWidget::HandleHealthChanged);
    UpdateStatPanel();
    if (FighterNameText) {
      FighterNameText->SetText(
          FText::FromString(BoundFighter->GetHumanReadableName()));
    }
  } else {
    if (HealthText) {
      HealthText->SetText(FText::GetEmpty());
    }
    if (AttackText) {
      AttackText->SetText(FText::GetEmpty());
    }
    if (MoveText) {
      MoveText->SetText(FText::GetEmpty());
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
  }
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

void UBattleHUDWidget::HandleHealthChanged(int32 NewHealth) {
  if (HealthText) {
    HealthText->SetText(FText::AsNumber(NewHealth));
  }
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
  if (MoveText) {
    MoveText->SetText(FText::AsNumber(BoundFighter->Stats.Movement));
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
}

void UBattleHUDWidget::SetRoundInfo(const FText &RoundLabel,
                                    const FText &InitiativeLabel) {
  if (RoundText) {
    RoundText->SetText(RoundLabel);
  }
  if (InitiativeText) {
    InitiativeText->SetText(InitiativeLabel);
  }
}

void UBattleHUDWidget::SetSelectedFighterName(const FText &Name) {
  if (FighterNameText) {
    FighterNameText->SetText(Name);
  }
}

void UBattleHUDWidget::SetActivateEnabled(bool bEnabled) {
  if (ActivateButton) {
    ActivateButton->SetIsEnabled(bEnabled);
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

void UBattleHUDWidget::ClearCommandPreviews() {
  bMoveSelected = false;
  bAttackSelected = false;
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    Grid->ClearHighlights();
  }
}

UGridOverlayComponent *UBattleHUDWidget::FindGridOverlay() const {
  if (UWorld *World = GetWorld()) {
    for (TActorIterator<AActor> It(World); It; ++It) {
      if (UGridOverlayComponent *Comp =
              It->FindComponentByClass<UGridOverlayComponent>()) {
        return Comp;
      }
    }
  }
  return nullptr;
}
