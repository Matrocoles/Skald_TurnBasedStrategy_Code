#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "PrepareForBattleWidget.generated.h"

class UButton;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPrepareForBattleClicked);

/**
 * Confirmation widget shown after an attack is approved on the world map.
 * Allows both participants to confirm they are ready before travelling to
 * the grid battle map.
 */
UCLASS(BlueprintType, Blueprintable)
class SKALD_API UPrepareForBattleWidget : public UUserWidget {
  GENERATED_BODY()

public:
  virtual void NativeOnInitialized() override;
  virtual void SynchronizeProperties() override;

  /** Configure the text displayed for each participant/territory pair. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void SetupBattleDetails(const FText &InAttackingPlayerID,
                          const FText &InAttackingTerritoryID,
                          const FText &InDefendingPlayerID,
                          const FText &InDefendingTerritoryID);

  /** Text block displaying the attacking player's ID. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *AttackingPlayerID;

  /** Text block displaying the attacking territory's ID. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *AttackingTerritoryID;

  /** Text block displaying the defending player's ID. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *DefendingPlayerID;

  /** Text block displaying the defending territory's ID. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *DefendingTerritoryID;

  /** Button that marks the local player as ready. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UButton *PrepareForBattleButton;

  /** Fired when the local player clicks the prepare button. */
  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle")
  FPrepareForBattleClicked OnPrepareButtonClicked;

  /** Default text displayed for the attacking player's ID field. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle")
  FText AttackingPlayerIDText = FText::FromString(TEXT("Attacker Player ID"));

  /** Default text displayed for the attacking territory field. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle")
  FText AttackingTerritoryIDText =
      FText::FromString(TEXT("Attacking Territory ID"));

  /** Default text displayed for the defending player's ID field. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle")
  FText DefendingPlayerIDText = FText::FromString(TEXT("Defender Player ID"));

  /** Default text displayed for the defending territory field. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle")
  FText DefendingTerritoryIDText =
      FText::FromString(TEXT("Defending Territory ID"));

protected:
  UFUNCTION()
  void HandlePrepareButtonClicked();

  void RefreshTextWidgets();

  /** Builds a minimal widget tree when no Blueprint is provided. */
  void BuildFallbackWidgetTree();
};

