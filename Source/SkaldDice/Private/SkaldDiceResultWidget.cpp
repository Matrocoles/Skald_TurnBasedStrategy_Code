#include "SkaldDiceResultWidget.h"
#include "Components/TextBlock.h"

USkaldDiceResultWidget::USkaldDiceResultWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PlayerPrefix = NSLOCTEXT("SkaldDice", "ResultPlayerPrefix", "Player: ");
    EnemyPrefix = NSLOCTEXT("SkaldDice", "ResultEnemyPrefix", "Enemy: ");
    EmptyValueText = NSLOCTEXT("SkaldDice", "ResultEmpty", "-");
    PrefixValueFormat = NSLOCTEXT("SkaldDice", "ResultPrefixFormat", "{0}{1}");
}

void USkaldDiceResultWidget::ShowResults(int32 PlayerResult, int32 EnemyResult)
{
    static const FText DefaultFormat = NSLOCTEXT("SkaldDice", "ResultPrefixFallback", "{0}{1}");
    if (PlayerResultText)
    {
        const FText Value = PlayerResult > INDEX_NONE ? FText::AsNumber(PlayerResult) : EmptyValueText;
        const FText& FormatText = !PrefixValueFormat.IsEmpty() ? PrefixValueFormat : DefaultFormat;
        const FText DisplayText = bShowPrefix ? FText::Format(FormatText, PlayerPrefix, Value) : Value;
        PlayerResultText->SetText(DisplayText);
    }

    if (EnemyResultText)
    {
        const FText Value = EnemyResult > INDEX_NONE ? FText::AsNumber(EnemyResult) : EmptyValueText;
        const FText& FormatText = !PrefixValueFormat.IsEmpty() ? PrefixValueFormat : DefaultFormat;
        const FText DisplayText = bShowPrefix ? FText::Format(FormatText, EnemyPrefix, Value) : Value;
        EnemyResultText->SetText(DisplayText);
    }

    ApplyResultColors();
}

void USkaldDiceResultWidget::SetResultColors(const FLinearColor& PlayerColor, const FLinearColor& EnemyColor)
{
    bHasPlayerResultColor = true;
    PlayerResultColor = FSlateColor(PlayerColor);
    bHasEnemyResultColor = true;
    EnemyResultColor = FSlateColor(EnemyColor);

    ApplyResultColors();
}

void USkaldDiceResultWidget::ApplyResultColors()
{
    if (PlayerResultText && bHasPlayerResultColor)
    {
        PlayerResultText->SetColorAndOpacity(PlayerResultColor);
    }

    if (EnemyResultText && bHasEnemyResultColor)
    {
        EnemyResultText->SetColorAndOpacity(EnemyResultColor);
    }
}
