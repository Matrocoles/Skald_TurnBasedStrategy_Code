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

  /** Delegate fired when the Activate button is pressed. */
  DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnActivatePressed);
  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Events")
  FOnActivatePressed OnActivatePressed;

  /** Delegate fired when the End Turn button is pressed. */
  DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEndTurnPressed);
  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Events")
  FOnEndTurnPressed OnEndTurnPressed;

  /** Move action button bound from the blueprint. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UButton *MoveButton;

  /** Attack action button bound from the blueprint. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UButton *AttackButton;

  /** Activate action button bound from the blueprint. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UButton *ActivateButton;

  /** End turn button bound from the blueprint. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UButton *EndTurnButton;

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

  /** Text displaying the fighter's name. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *FighterNameText;

  /** Text displaying the current round. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *RoundText;

  /** Text displaying initiative winner. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *InitiativeText;

  /** Update the round and initiative labels. */
  void SetRoundInfo(const FText &RoundLabel, const FText &InitiativeLabel);

  /** Update the fighter name label. */
  void SetSelectedFighterName(const FText &Name);

  /** Enable or disable the Activate button. */
  void SetActivateEnabled(bool bEnabled);

  /** Enable or disable the End Turn button. */
  void SetEndTurnEnabled(bool bEnabled);

  /** Toggle visibility for the End Turn button. */
  void SetEndTurnVisibility(bool bVisible);

  /** Clear any preview highlights tracked by the widget. */
  void ClearCommandPreviews();

private:
  /** Callback when MoveButton is pressed. */
  UFUNCTION()
  void HandleMovePressed();

  /** Callback when AttackButton is pressed. */
  UFUNCTION()
  void HandleAttackPressed();

  /** Callback when ActivateButton is pressed. */
  UFUNCTION()
  void HandleActivatePressed();

  /** Callback when EndTurnButton is pressed. */
  UFUNCTION()
  void HandleEndTurnPressed();

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

