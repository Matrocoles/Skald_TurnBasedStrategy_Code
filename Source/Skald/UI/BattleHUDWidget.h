#pragma once

#include "Blueprint/UserWidget.h"
#include "GridBattleManager.h"
#include "TimerManager.h"
#include "BattleHUDWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class AFighterPawn;
class UGridOverlayComponent;
class UTexture2D;
class UCombatFloaterPoolSubsystem;
class UW_DiceResolutionPanel;
class UW_FloatingText;

struct FBattleActiveFloater {
  TWeakObjectPtr<UW_FloatingText> Floater;
  FVector AnchorLocation = FVector::ZeroVector;
  FVector2D InitialOffset = FVector2D::ZeroVector;
  float HorizontalDirection = 1.f;
  float Lifetime = 1.4f;
  float FadeDuration = 0.35f;
  float Elapsed = 0.f;
  float Scale = 1.f;
};

/**
 * HUD widget displayed during grid battles.
 */
UCLASS(BlueprintType, Blueprintable)
class SKALD_API UBattleHUDWidget : public UUserWidget {
  GENERATED_BODY()

public:
  UBattleHUDWidget(const FObjectInitializer &ObjectInitializer);

  virtual void NativeConstruct() override;
  virtual void NativeTick(const FGeometry &MyGeometry,
                          float InDeltaTime) override;
  virtual void NativeDestruct() override;

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

  /** Delegate fired when the initiative roll button is pressed. */
  DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInitiativeRollRequested);
  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Events")
  FOnInitiativeRollRequested OnInitiativeRollRequested;

  DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
      FOnResolutionComplete, AFighterPawn *, Attacker, AFighterPawn *, Defender,
      const FDiceRollResult &, Result);
  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Events")
  FOnResolutionComplete OnResolutionComplete;

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

  /** Initiative roll button bound from the blueprint. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UButton *RollInitiativeButton;

  /** Text displaying current health. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *HealthText;

  /** Text displaying attack damage. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *AttackText;

  /** Text displaying critical hit damage. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *CriticalDamageText;

  /** Text displaying movement range. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *MoveText;

  /** Text displaying remaining actions. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *ActionsText;

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

  /** Text displaying the fighter's identifier. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *FighterNameText;

  /** Image displaying the fighter's portrait. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UImage *FighterImage;

  /** Text displaying the current round. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *RoundText;

  /** Text displaying initiative winner. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *InitiativeText;

  /** Text displaying whose turn is currently active. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *PlayersTurnText;

  /** Prompt displayed before rolling initiative. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *InitiativePromptText;

  /** Image used to display a temporary dice roll result. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UImage *DiceRollerImage;

  /** Background image shown behind the dice roller when active. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UImage *DiceBoardImage;

  /** Panel that reveals per-die outcomes in sequence. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UW_DiceResolutionPanel *DiceResolutionPanel;

  /** Textures representing dice faces, indexed from 1 to 6. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Battle|Dice")
  TArray<TObjectPtr<UTexture2D>> DiceFaceTextures;

  /** Display a dice face corresponding to the supplied roll value. */
  void ShowDiceRoll(int32 RollValue, float DisplayDuration = 1.f);

  /** Display the initiative prompt and roll button. */
  void ShowInitiativePrompt(const FText &PromptText);

  /** Hide the initiative prompt and roll button. */
  void HideInitiativePrompt();

  /** Update the round and initiative labels. */
  void SetRoundInfo(const FText &RoundLabel, const FText &InitiativeLabel);

  /** Update the label that shows whose turn is active. */
  void SetPlayersTurnLabel(const FText &PlayerLabel);

  /** Update the fighter identifier label and portrait. */
  void SetSelectedFighterName(const FText &Name);

  /** Enable or disable the Activate button. */
  void SetActivateEnabled(bool bEnabled);

  /** Toggle visibility for the Activate button. */
  void SetActivateVisibility(bool bVisible);

  /** Enable or disable the End Turn button. */
  void SetEndTurnEnabled(bool bEnabled);

  /** Toggle visibility for the End Turn button. */
  void SetEndTurnVisibility(bool bVisible);

  /** Toggle visibility for the Move and Attack buttons. */
  void SetActionButtonsVisibility(bool bVisible);

  /** Clear any preview highlights tracked by the widget. */
  void ClearCommandPreviews();

  /** Display floating text anchored around a world location. */
  void ShowCombatFloater(const FVector &WorldLocation, const FText &Message,
                         const FLinearColor &Tint, float Scale = 1.f,
                         bool bUseMissStyling = false,
                         float LifetimeOverride = -1.f);

  /** Helper used by attack resolution events to show damage/miss floaters. */
  void ShowAttackResultFloater(AFighterPawn *Target,
                               const FDiceRollResult &Result);

  /** Queue a dice resolution sequence for presentation on the panel. */
  void QueueDiceResolution(AFighterPawn *Attacker, AFighterPawn *Defender,
                           const FDiceRollResult &Result);

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

  /** Callback when RollInitiativeButton is pressed. */
  UFUNCTION()
  void HandleInitiativeRollPressed();

  /** Update all stat text panels from the bound fighter. */
  void UpdateStatPanel();

  /** Update the visibility state of action buttons. */
  void UpdateActionButtonVisibility();

  /** Respond to health changes from the fighter. */
  UFUNCTION()
  void HandleHealthChanged(int32 NewHealth);

  /** Respond to action count changes from the fighter. */
  UFUNCTION()
  void HandleActionsChanged(int32 NewActions);

  /** Find the grid overlay component in the world. */
  UGridOverlayComponent *FindGridOverlay() const;

  /** Hide the dice roller image after the timer elapses. */
  void HideDiceRoller();

  /** Hide the initiative text after the timer elapses. */
  void HideInitiativeText();

  /** Reveal the initiative roll button after a short delay. */
  void RevealInitiativeRollButton();

  void ProcessNextDiceResolution();

  UFUNCTION()
  void HandleDicePanelResolved(const FDiceRollResult &Result);

  void UpdateCombatFloaters(float DeltaSeconds);
  void ReleaseFloaterAtIndex(int32 Index);
  UCombatFloaterPoolSubsystem *ResolveFloaterPool();

  /** Class used to spawn floaters via the shared subsystem. */
  UPROPERTY(EditAnywhere, Category = "Skald|Battle|Floaters")
  TSubclassOf<UW_FloatingText> FloaterWidgetClass;

  /** Lifetime for damage/miss floaters. */
  UPROPERTY(EditAnywhere, Category = "Skald|Battle|Floaters")
  float FloaterLifetime = 1.4f;

  /** Portion of the lifetime reserved for the fade. */
  UPROPERTY(EditAnywhere, Category = "Skald|Battle|Floaters")
  float FloaterFadeDuration = 0.35f;

  /** Height of the arc applied to floaters. */
  UPROPERTY(EditAnywhere, Category = "Skald|Battle|Floaters")
  float FloaterArcHeight = 110.f;

  /** Horizontal drift in pixels over the lifetime. */
  UPROPERTY(EditAnywhere, Category = "Skald|Battle|Floaters")
  float FloaterHorizontalDrift = 36.f;

  /** Clamp margin for projected positions. */
  UPROPERTY(EditAnywhere, Category = "Skald|Battle|Floaters")
  float FloaterClampMargin = 28.f;

  /** Default colour for successful hits. */
  UPROPERTY(EditAnywhere, Category = "Skald|Battle|Floaters")
  FLinearColor HitFloaterColor = FLinearColor(0.12f, 0.76f, 0.45f);

  /** Default colour for critical hits. */
  UPROPERTY(EditAnywhere, Category = "Skald|Battle|Floaters")
  FLinearColor CriticalFloaterColor = FLinearColor(0.98f, 0.78f, 0.15f);

  /** Default colour for misses. */
  UPROPERTY(EditAnywhere, Category = "Skald|Battle|Floaters")
  FLinearColor MissFloaterColor = FLinearColor(0.7f, 0.7f, 0.74f);

  /** Active floating text widgets managed by the battle HUD. */
  TArray<FBattleActiveFloater> ActiveFloaters;

  TWeakObjectPtr<UCombatFloaterPoolSubsystem> CachedFloaterPool;

  struct FBattleQueuedDiceResolution {
    TWeakObjectPtr<AFighterPawn> Attacker;
    TWeakObjectPtr<AFighterPawn> Defender;
    FDiceRollResult Result;
  };

  TArray<FBattleQueuedDiceResolution> PendingDiceResolutions;
  bool bDiceResolutionActive = false;
  FBattleQueuedDiceResolution ActiveDiceResolution;

  /** Whether movement preview is currently shown. */
  bool bMoveSelected = false;

  /** Whether attack preview is currently shown. */
  bool bAttackSelected = false;

  /** Whether action buttons are allowed to be displayed. */
  bool bActionButtonsUnlocked = false;

  /** Fighter currently bound to the HUD. */
  UPROPERTY()
  AFighterPawn *BoundFighter;

  /** Timer managing dice roll visibility. */
  FTimerHandle DiceRollerHideTimer;

  /** Timer managing initiative label visibility. */
  FTimerHandle InitiativeHideTimer;

  /** Timer delaying the display of the initiative roll button. */
  FTimerHandle InitiativeRollButtonDelayTimer;
};

