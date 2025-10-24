#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "PrepareForBattleWidget.generated.h"

class UButton;
class UTextBlock;
class UImage;
class UTexture2D;

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
  void SetupBattleDetails(const FText &InAttackingPlayerName,
                          const FText &InAttackingTerritoryName,
                          UTexture2D *InAttackingFactionIcon,
                          const FText &InDefendingPlayerName,
                          const FText &InDefendingTerritoryName,
                          UTexture2D *InDefendingFactionIcon);

  /** Text block displaying the attacking player's name. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *AttackingPlayerName;

  /** Text block displaying the attacking territory's title. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *AttackingTerritoryName;

  /** Text block displaying the defending player's name. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *DefendingPlayerName;

  /** Text block displaying the defending territory's title. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *DefendingTerritoryName;

  /** Image used to display the attacking faction emblem. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UImage *AttackingFactionEmblem;

  /** Image used to display the defending faction emblem. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UImage *DefendingFactionEmblem;

  /** Button that marks the local player as ready. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UButton *PrepareForBattleButton;

  /** Fired when the local player clicks the prepare button. */
  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle")
  FPrepareForBattleClicked OnPrepareButtonClicked;

  /** Default text displayed for the attacking player's ID field. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle")
  FText AttackingPlayerNameText =
      FText::FromString(TEXT("Attacking Player"));

  /** Default text displayed for the attacking territory field. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle")
  FText AttackingTerritoryNameText =
      FText::FromString(TEXT("Attacking Territory"));

  /** Default text displayed for the defending player's name field. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle")
  FText DefendingPlayerNameText =
      FText::FromString(TEXT("Defending Player"));

  /** Default text displayed for the defending territory field. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle")
  FText DefendingTerritoryNameText =
      FText::FromString(TEXT("Defending Territory"));

protected:
  UFUNCTION()
  void HandlePrepareButtonClicked();

  void RefreshTextWidgets();

  /** Builds a minimal widget tree when no Blueprint is provided. */
  void BuildFallbackWidgetTree();

  /** Cached texture references used when updating emblem widgets. */
  TWeakObjectPtr<UTexture2D> AttackingFactionTexture;
  TWeakObjectPtr<UTexture2D> DefendingFactionTexture;
};

