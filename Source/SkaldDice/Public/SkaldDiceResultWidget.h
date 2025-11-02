#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkaldDiceResultWidget.generated.h"

class UTextBlock;

UCLASS()
class SKALDDICE_API USkaldDiceResultWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    USkaldDiceResultWidget(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable, Category = "Dice")
    void ShowResults(int32 PlayerResult, int32 EnemyResult);

protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice")
    FText PlayerPrefix;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice")
    FText EnemyPrefix;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice")
    bool bShowPrefix = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice")
    FText EmptyValueText;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice", meta = (EditCondition = "bShowPrefix"))
    FText PrefixValueFormat;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> PlayerResultText;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> EnemyResultText;
};
