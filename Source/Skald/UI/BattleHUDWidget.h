#pragma once

#include "Blueprint/UserWidget.h"
#include "BattleHUDWidget.generated.h"

class UButton;
class UTextBlock;
class AFighterPawn;
class UGridOverlayComponent;

/**
 * HUD widget displayed during grid battles.
 */
UCLASS(BlueprintType, Blueprintable)
class SKALD_API UBattleHUDWidget : public UUserWidget {
  GENERATED_BODY()

public:
  virtual void NativeConstruct() override;

  /** Refresh all stat text from the currently bound fighter. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void RefreshStats();

  /** Bind this HUD to a fighter so its stats are displayed. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void BindToFighter(AFighterPawn *Fighter);

  /** Delegate fired when the Move button is pressed. */
  DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMovePressed);
  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Events")
  FOnMovePressed OnMovePressed;

  /** Delegate fired when the Attack button is pressed. */
  DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAttackPressed);
  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Events")
  FOnAttackPressed OnAttackPressed;

  /** Move action button bound from the blueprint. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UButton *MoveButton;

  /** Attack action button bound from the blueprint. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UButton *AttackButton;

  /** Text displaying current health. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *HealthText;

  /** Text displaying attack damage. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *AttackText;

  /** Text displaying movement range. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *MoveText;

  /** Text displaying strength value. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *StrengthText;

  /** Text displaying defence value. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *DefenceText;

  /** Text displaying attack range. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *AttackRangeText;

  /** Text displaying number of attack dice. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *AttackDiceText;

private:
  /** Callback when MoveButton is pressed. */
  UFUNCTION()
  void HandleMovePressed();

  /** Callback when AttackButton is pressed. */
  UFUNCTION()
  void HandleAttackPressed();

  /** Update all stat text panels from the bound fighter. */
  void UpdateStatPanel();

  /** Respond to health changes from the fighter. */
  UFUNCTION()
  void HandleHealthChanged(int32 NewHealth);

  /** Find the grid overlay component in the world. */
  UGridOverlayComponent *FindGridOverlay() const;

  /** Whether movement preview is currently shown. */
  bool bMoveSelected = false;

  /** Whether attack preview is currently shown. */
  bool bAttackSelected = false;

  /** Fighter currently bound to the HUD. */
  UPROPERTY()
  AFighterPawn *BoundFighter;
};

