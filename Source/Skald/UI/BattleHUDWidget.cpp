#include "UI/BattleHUDWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "FighterPawn.h"
#include "GridOverlayComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void UBattleHUDWidget::NativeConstruct() {
  Super::NativeConstruct();

  if (MoveButton) {
    MoveButton->OnClicked.AddDynamic(this, &UBattleHUDWidget::HandleMovePressed);
  }
  if (AttackButton) {
    AttackButton->OnClicked.AddDynamic(this, &UBattleHUDWidget::HandleAttackPressed);
  }
}

void UBattleHUDWidget::BindToFighter(AFighterPawn *Fighter) {
  if (BoundFighter) {
    BoundFighter->OnHealthChanged.RemoveDynamic(this,
                                                &UBattleHUDWidget::HandleHealthChanged);
  }
  BoundFighter = Fighter;
  if (BoundFighter) {
    BoundFighter->OnHealthChanged.AddDynamic(this,
                                             &UBattleHUDWidget::HandleHealthChanged);
    UpdateStatPanel();
  }
}

void UBattleHUDWidget::HandleMovePressed() {
  OnMovePressed.Broadcast();
  if (!BoundFighter) {
    return;
  }
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    FIntPoint Origin = Grid->WorldToGrid(BoundFighter->GetActorLocation());
    int32 Range = BoundFighter->Stats.Movement;
    for (int32 X = -Range; X <= Range; ++X) {
      for (int32 Y = -Range; Y <= Range; ++Y) {
        if (FMath::Abs(X) + FMath::Abs(Y) <= Range) {
          Grid->HighlightCell(Origin + FIntPoint(X, Y), FColor::Green, 1.f);
        }
      }
    }
  }
}

void UBattleHUDWidget::HandleAttackPressed() {
  OnAttackPressed.Broadcast();
  if (!BoundFighter) {
    return;
  }
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    FIntPoint Origin = Grid->WorldToGrid(BoundFighter->GetActorLocation());
    int32 Range = BoundFighter->Stats.AttackRange;
    for (int32 X = -Range; X <= Range; ++X) {
      for (int32 Y = -Range; Y <= Range; ++Y) {
        if (FMath::Abs(X) + FMath::Abs(Y) <= Range) {
          Grid->HighlightCell(Origin + FIntPoint(X, Y), FColor::Red, 1.f);
        }
      }
    }
  }
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
