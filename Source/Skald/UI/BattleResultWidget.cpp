#include "UI/BattleResultWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/SlateFontInfo.h"

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
    if (!BattleResultText) {
      BattleResultText = WidgetTree->ConstructWidget<UTextBlock>(
          UTextBlock::StaticClass(), TEXT("BattleResultText"));
      BattleResultText->SetJustification(ETextJustify::Center);
      FSlateFontInfo ResultFont = BattleResultText->Font;
      ResultFont.Size = 72;
      BattleResultText->SetFont(ResultFont);
      Box->AddChildToVerticalBox(BattleResultText);
    }

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

  if (BattleResultText) {
    if (bPlayerWon) {
      BattleResultText->SetText(
          NSLOCTEXT("BattleResultWidget", "VictoryLarge", "Victory!!"));
      BattleResultText->SetColorAndOpacity(FSlateColor(FLinearColor::Green));
    } else if (bPlayerLost) {
      BattleResultText->SetText(
          NSLOCTEXT("BattleResultWidget", "DefeatLarge", "Defeat!!"));
      BattleResultText->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
    } else {
      BattleResultText->SetText(FText::GetEmpty());
      BattleResultText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    }
  }

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

  if (bPlayerWon) {
    if (VictorySound) {
      UGameplayStatics::PlaySound2D(this, VictorySound);
    }
  } else if (bPlayerLost) {
    if (DefeatSound) {
      UGameplayStatics::PlaySound2D(this, DefeatSound);
    }
  }
}
