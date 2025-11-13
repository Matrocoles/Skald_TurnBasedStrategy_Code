#include "UI/BattleResultWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Fonts/SlateFontInfo.h"

void UBattleResultWidget::NativeConstruct() {
  Super::NativeConstruct();
  EnsureLayout();

  if (CloseButton &&
      !CloseButton->OnClicked.IsAlreadyBound(this, &UBattleResultWidget::HandleCloseClicked)) {
    CloseButton->OnClicked.AddDynamic(this, &UBattleResultWidget::HandleCloseClicked);
  }
}

void UBattleResultWidget::NativeDestruct() {
  if (CloseButton) {
    CloseButton->OnClicked.RemoveAll(this);
  }

  Super::NativeDestruct();
}

void UBattleResultWidget::EnsureLayout() {
  if (ResultsText && PlayersName && PlayersFaction && Casualties &&
      EnemyPlayersName && EnemyPlayersFaction && EnemyCasualties && CloseButton) {
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
    auto EnsureTextBlock = [this, Box](UTextBlock *&TextPtr, const TCHAR *Name,
                                       int32 FontSize) {
      if (TextPtr) {
        return;
      }

      TextPtr = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
      if (!TextPtr) {
        return;
      }

      TextPtr->SetJustification(ETextJustify::Center);
      if (FontSize > 0) {
        FSlateFontInfo FontInfo = TextPtr->GetFont();
        FontInfo.Size = FontSize;
        TextPtr->SetFont(FontInfo);
      }

      Box->AddChildToVerticalBox(TextPtr);
    };

    EnsureTextBlock(BattleResultText, TEXT("BattleResultText"), 72);
    EnsureTextBlock(ResultsText, TEXT("ResultsText"), 40);
    EnsureTextBlock(PlayersName, TEXT("PlayersName"), 32);
    EnsureTextBlock(PlayersFaction, TEXT("PlayersFaction"), 28);
    EnsureTextBlock(Casualties, TEXT("Casualties"), 28);
    EnsureTextBlock(EnemyPlayersName, TEXT("EnemyPlayersName"), 32);
    EnsureTextBlock(EnemyPlayersFaction, TEXT("EnemyPlayersFaction"), 28);
    EnsureTextBlock(EnemyCasualties, TEXT("EnemyCasualties"), 28);

    if (!CloseButton) {
      CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Close"));
      if (CloseButton) {
        if (UTextBlock *CloseLabel = WidgetTree->ConstructWidget<UTextBlock>(
                UTextBlock::StaticClass(), TEXT("CloseLabel"))) {
          CloseLabel->SetJustification(ETextJustify::Center);
          CloseLabel->SetText(NSLOCTEXT("BattleResultWidget", "Close", "Close"));
          CloseButton->AddChild(CloseLabel);
        }

        Box->AddChildToVerticalBox(CloseButton);
      }
    }
  }
}

void UBattleResultWidget::SetBattleOutcome(bool bPlayerWon, bool bPlayerLost,
                                           int32 AttackerCasualties,
                                           int32 DefenderCasualties,
                                           const FLinearColor &PlayerColor,
                                           const FText &PlayerName,
                                           const FText &PlayerFaction,
                                           const FText &EnemyPlayerName,
                                           const FText &EnemyFaction,
                                           bool bPlayerWasAttacker) {
  EnsureLayout();

  if (BattleResultText) {
    if (bPlayerWon) {
      BattleResultText->SetText(
          NSLOCTEXT("BattleResultWidget", "VictoryLarge", "Victory!!"));
      BattleResultText->SetColorAndOpacity(FSlateColor(PlayerColor));
    } else if (bPlayerLost) {
      BattleResultText->SetText(
          NSLOCTEXT("BattleResultWidget", "DefeatLarge", "Defeat!!"));
      BattleResultText->SetColorAndOpacity(FSlateColor(PlayerColor));
    } else {
      BattleResultText->SetText(FText::GetEmpty());
      BattleResultText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    }
  }

  const int32 PlayerCasualties = bPlayerWasAttacker ? AttackerCasualties : DefenderCasualties;
  const int32 EnemyCasualtyCount = bPlayerWasAttacker ? DefenderCasualties : AttackerCasualties;

  if (ResultsText) {
    FText OutcomeLabel;
    if (bPlayerWon) {
      OutcomeLabel = NSLOCTEXT("BattleResultWidget", "Victory", "Victory!");
    } else if (bPlayerLost) {
      OutcomeLabel = NSLOCTEXT("BattleResultWidget", "Defeat", "Defeat");
    } else {
      OutcomeLabel = NSLOCTEXT("BattleResultWidget", "Complete", "Battle Complete");
    }
    ResultsText->SetText(OutcomeLabel);
  }

  if (PlayersName) {
    PlayersName->SetText(PlayerName);
  }

  if (PlayersFaction) {
    PlayersFaction->SetText(PlayerFaction);
  }

  if (Casualties) {
    Casualties->SetText(FText::AsNumber(PlayerCasualties));
  }

  if (EnemyPlayersName) {
    EnemyPlayersName->SetText(EnemyPlayerName);
  }

  if (EnemyPlayersFaction) {
    EnemyPlayersFaction->SetText(EnemyFaction);
  }

  if (EnemyCasualties) {
    EnemyCasualties->SetText(FText::AsNumber(EnemyCasualtyCount));
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

void UBattleResultWidget::HandleCloseClicked() {
  RemoveFromParent();
}
