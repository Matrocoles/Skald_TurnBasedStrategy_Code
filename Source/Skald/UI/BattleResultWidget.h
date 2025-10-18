#pragma once

#include "Blueprint/UserWidget.h"
#include "BattleResultWidget.generated.h"

class UTextBlock;
class USoundBase;

/** Simple widget that displays battle outcome text and casualty totals. */
UCLASS()
class SKALD_API UBattleResultWidget : public UUserWidget {
  GENERATED_BODY()

public:
  virtual void NativeConstruct() override;

  /** Update the displayed outcome text. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void SetBattleOutcome(bool bPlayerWon, bool bPlayerLost, int32 AttackerCasualties,
                        int32 DefenderCasualties);

private:
  void EnsureLayout();

  UPROPERTY()
  UTextBlock *BattleResultText = nullptr;

  UPROPERTY()
  UTextBlock *OutcomeText = nullptr;

  UPROPERTY()
  UTextBlock *CasualtyText = nullptr;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle", meta = (AllowPrivateAccess = "true"))
  USoundBase *VictorySound = nullptr;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle", meta = (AllowPrivateAccess = "true"))
  USoundBase *DefeatSound = nullptr;
};
