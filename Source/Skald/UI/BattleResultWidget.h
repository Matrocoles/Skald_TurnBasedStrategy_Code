#pragma once

#include "Blueprint/UserWidget.h"
#include "BattleResultWidget.generated.h"

class UTextBlock;
class USoundBase;
class UButton;

/** Simple widget that displays battle outcome text and casualty totals. */
UCLASS()
class SKALD_API UBattleResultWidget : public UUserWidget {
  GENERATED_BODY()

public:
  virtual void NativeConstruct() override;
  virtual void NativeDestruct() override;

  /** Update the displayed outcome text. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void SetBattleOutcome(bool bPlayerWon, bool bPlayerLost, int32 AttackerCasualties,
                        int32 DefenderCasualties, const FLinearColor &PlayerColor,
                        const FText &PlayerName, const FText &PlayerFaction,
                        const FText &EnemyPlayerName, const FText &EnemyFaction,
                        bool bPlayerWasAttacker);

private:
  void EnsureLayout();
  UFUNCTION()
  void HandleCloseClicked();

  UPROPERTY(meta = (BindWidgetOptional))
  UTextBlock *BattleResultText = nullptr;

  UPROPERTY(meta = (BindWidgetOptional))
  UTextBlock *ResultsText = nullptr;

  UPROPERTY(meta = (BindWidgetOptional))
  UTextBlock *PlayersName = nullptr;

  UPROPERTY(meta = (BindWidgetOptional))
  UTextBlock *PlayersFaction = nullptr;

  UPROPERTY(meta = (BindWidgetOptional))
  UTextBlock *Casualties = nullptr;

  UPROPERTY(meta = (BindWidgetOptional))
  UTextBlock *EnemyPlayersName = nullptr;

  UPROPERTY(meta = (BindWidgetOptional))
  UTextBlock *EnemyPlayersFaction = nullptr;

  UPROPERTY(meta = (BindWidgetOptional))
  UTextBlock *EnemyCasualties = nullptr;

  UPROPERTY(meta = (BindWidgetOptional))
  UButton *CloseButton = nullptr;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle", meta = (AllowPrivateAccess = "true"))
  USoundBase *VictorySound = nullptr;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle", meta = (AllowPrivateAccess = "true"))
  USoundBase *DefeatSound = nullptr;
};
