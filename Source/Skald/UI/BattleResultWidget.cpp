#include "UI/BattleResultWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

void UBattleResultWidget::NativeConstruct() {
  Super::NativeConstruct();
  EnsureLayout();
}

void UBattleResultWidget::EnsureLayout() {
  if (OutcomeText && CasualtyText) {
    return;
  }

  if (!WidgetTree) {
    return;
  }

  if (!WidgetTree->RootWidget) {
    UVerticalBox *Box = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("BattleResultRoot"));
    WidgetTree->RootWidget = Box;
  }

  if (UVerticalBox *Box = Cast<UVerticalBox>(WidgetTree->RootWidget)) {
    if (!OutcomeText) {
      OutcomeText = WidgetTree->ConstructWidget<UTextBlock>(
          UTextBlock::StaticClass(), TEXT("OutcomeText"));
      OutcomeText->SetJustification(ETextJustify::Center);
      Box->AddChildToVerticalBox(OutcomeText);
    }

    if (!CasualtyText) {
      CasualtyText = WidgetTree->ConstructWidget<UTextBlock>(
          UTextBlock::StaticClass(), TEXT("CasualtyText"));
      CasualtyText->SetJustification(ETextJustify::Center);
      Box->AddChildToVerticalBox(CasualtyText);
    }
  }
}

void UBattleResultWidget::SetBattleOutcome(bool bPlayerWon, bool bPlayerLost,
                                           int32 AttackerCasualties,
                                           int32 DefenderCasualties) {
  EnsureLayout();

  if (OutcomeText) {
    FText OutcomeLabel;
    if (bPlayerWon) {
      OutcomeLabel = NSLOCTEXT("BattleResultWidget", "Victory", "Victory!");
    } else if (bPlayerLost) {
      OutcomeLabel = NSLOCTEXT("BattleResultWidget", "Defeat", "Defeat");
    } else {
      OutcomeLabel = NSLOCTEXT("BattleResultWidget", "Complete", "Battle Complete");
    }
    OutcomeText->SetText(OutcomeLabel);
  }

  if (CasualtyText) {
    const FText CasualtyLabel = FText::Format(
        NSLOCTEXT("BattleResultWidget", "CasualtiesFormat",
                  "Attacker losses: {0}\nDefender losses: {1}"),
        FText::AsNumber(AttackerCasualties),
        FText::AsNumber(DefenderCasualties));
    CasualtyText->SetText(CasualtyLabel);
  }
}
