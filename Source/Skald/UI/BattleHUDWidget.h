#pragma once

#include "Blueprint/UserWidget.h"
#include "GridBattleManager.h"
#include "TimerManager.h"
#include "UI/W_DiceResolutionPanel.h"
#include "Abilities/SkaldAbilityComponent.h"
#include "BattleHUDWidget.generated.h"

class UButton;
class UImage;
class UCanvasRenderTarget2D;
class UTextBlock;
class UScrollBox;
class AFighterPawn;
class UGridOverlayComponent;
class UTexture2D;
class USoundBase;
class UCombatFloaterPoolSubsystem;
class UW_DiceResolutionPanel;
class UW_FloatingText;
class ULockedInFighterEntryWidget;

USTRUCT(BlueprintType)
struct SKALD_API FSkaldFloaterStyle {
  GENERATED_BODY();

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle|Floaters")
  FLinearColor Color = FLinearColor(0.98f, 0.78f, 0.15f);

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle|Floaters",
            meta = (ClampMin = "0.1"))
  float Scale = 1.0f;
};

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

USTRUCT(BlueprintType)
struct SKALD_API FBattleAbilitySlotDisplay {
  GENERATED_BODY()

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Battle|Abilities")
  ESkaldAbilitySlot Slot = ESkaldAbilitySlot::Ability1;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Battle|Abilities")
  FSkaldAbilityDefinition Definition;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Battle|Abilities")
  int32 CooldownRemaining = 0;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Battle|Abilities")
  bool bIsOnCooldown = false;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Battle|Abilities")
  bool bHasBeenUsed = false;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Battle|Abilities")
  bool bCanActivate = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAbilityDisplayChanged,
                                            const FSkaldAbilityDefinition &,
                                            PassiveAbility,
                                            const TArray<FBattleAbilitySlotDisplay> &,
                                            ActiveSlots);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilitySlotPressed,
                                            ESkaldAbilitySlot, Slot);

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
  DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
      FOnDiceOutcomeRevealed, AFighterPawn *, Attacker, AFighterPawn *, Defender,
      const FDiceRollOutcome &, Outcome, int32, RevealIndex);
  DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLockedInFighterEntrySelected,
                                              AFighterPawn *, Fighter);
  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Events")
  FOnResolutionComplete OnResolutionComplete;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Events")
  FOnDiceOutcomeRevealed OnDiceOutcomeRevealed;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Events")
  FOnLockedInFighterEntrySelected OnLockedInFighterEntrySelected;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Abilities")
  FOnAbilityDisplayChanged OnAbilityDisplayChanged;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Abilities")
  FOnAbilitySlotPressed OnAbilitySlotPressed;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Battle|Abilities")
  FSkaldAbilityDefinition PassiveAbilityDefinition;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Battle|Abilities")
  TArray<FBattleAbilitySlotDisplay> AbilitySlotDefinitions;

  /** Returns true while dice resolutions or combat floaters are still animating. */
  UFUNCTION(BlueprintPure, Category = "Skald|Battle")
  bool IsCombatPresentationActive() const;

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

  /** Optional UI button for the first ability slot. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UButton *AbilityButton1;

  /** Optional UI button for the second ability slot. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UButton *AbilityButton2;

  /** Optional UI button for the third ability slot. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UButton *AbilityButton3;

  /** Optional image displayed for ability slot 1. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UImage *AbilityIcon1;

  /** Optional image displayed for ability slot 2. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UImage *AbilityIcon2;

  /** Optional image displayed for ability slot 3. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UImage *AbilityIcon3;

  /** Optional label displayed under ability slot 1. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *AbilityLabel1;

  /** Optional label displayed under ability slot 2. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *AbilityLabel2;

  /** Optional label displayed under ability slot 3. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *AbilityLabel3;

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

  /** Text displaying the territory currently being contested. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UTextBlock *TerritoryText;

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

  /** Render target used to draw fallback dice faces for high-value rolls. */
  UPROPERTY(Transient)
  TObjectPtr<UCanvasRenderTarget2D> DiceRollerRenderTarget;

  /** Scroll box containing locked-in fighter entries. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UScrollBox *ScrollBox_LockedInFightersList;

  /** Widget class used for locked-in fighter list entries. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Battle")
  TSubclassOf<ULockedInFighterEntryWidget> LockedInFighterEntryClass;

  /** Populate the locked-in fighter list with the provided fighters. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void SetLockedInFighters(const TArray<AFighterPawn *> &Fighters);

  /** Clear the locked-in fighter list and associated bindings. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void ClearLockedInFighterList();

  /** Highlight the entry representing the selected fighter. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void SetHighlightedLockedInFighter(AFighterPawn *Fighter);

  /** Keep the locked-in list highlight in sync with controller selection changes. */
  UFUNCTION()
  void HandleSelectedFighterChanged(AFighterPawn *Fighter);

  /** Highlight the entry for the fighter whose activation is in progress. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void SetActiveLockedInFighter(AFighterPawn *Fighter);

  /** Refresh dimming state for all entries based on turn completion. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void RefreshLockedInFighterTurnStates();

  /** Panel that reveals per-die outcomes in sequence. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UW_DiceResolutionPanel *DiceResolutionPanel;

  /** Textures representing dice faces, indexed from 1 to 6. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Battle|Dice")
  TArray<TObjectPtr<UTexture2D>> DiceFaceTextures;

  /** Default layout overrides applied to the attack dice reveal panel. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Battle|Dice")
  FDiceResolutionPanelLayout DefaultDiceResolutionPanelLayout;

  /** Optional sound effect to play when a dice roll is shown. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Battle|Dice")
  TObjectPtr<USoundBase> DiceRollSound;

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

  /** Update the label that shows the contested territory. */
  void SetTerritoryName(const FText &TerritoryLabel);

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

  /** Display a short-lived MISS tag for the supplied defender. */
  void ShowMissTag(class AFighterPawn *Target);

  /** Helper used by attack resolution events to show damage/miss floaters. */
  UFUNCTION(BlueprintCallable, Category="Skald|HUD")
  void ShowAttackResultFloater(class AFighterPawn *Target,
                               const FDiceRollResult &Result);

  /** Queue a dice resolution sequence for presentation on the panel. */
  void QueueDiceResolution(AFighterPawn *Attacker, AFighterPawn *Defender,
                           const FDiceRollResult &Result);

  /** Override the default layout values at runtime. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle|Dice")
  void SetDefaultDiceResolutionPanelLayout(const FDiceResolutionPanelLayout &Layout);

  /** Apply a layout override immediately without mutating the defaults. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle|Dice")
  void ApplyDiceResolutionPanelLayout(const FDiceResolutionPanelLayout &Layout);

  /**
   * Resolve the layout that should be applied before revealing the current dice roll.
   * Blueprint implementations can customize placement on a per-attack basis.
   */
  UFUNCTION(BlueprintNativeEvent, Category = "Skald|Battle|Dice")
  FDiceResolutionPanelLayout ResolveDiceResolutionPanelLayout(
      AFighterPawn *Attacker, AFighterPawn *Defender, const FDiceRollResult &Result) const;
  FDiceResolutionPanelLayout ResolveDiceResolutionPanelLayout_Implementation(
      AFighterPawn *Attacker, AFighterPawn *Defender, const FDiceRollResult &Result) const;

private:
  void PruneInvalidLockedInEntries();
  ULockedInFighterEntryWidget *FindLockedInEntry(AFighterPawn *Fighter) const;
  ULockedInFighterEntryWidget *FindOrCreateLockedInEntry(AFighterPawn *Fighter);
  void HandleLockedInEntryClicked(AFighterPawn *Fighter);
  void HandleLockedInEntryRemoved(AFighterPawn *Fighter);
  void RemoveLockedInEntry(AFighterPawn *Fighter);

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

  /** Callback when AbilityButton1 is pressed. */
  UFUNCTION()
  void HandleAbilityButtonPressedSlot1();

  /** Callback when AbilityButton2 is pressed. */
  UFUNCTION()
  void HandleAbilityButtonPressedSlot2();

  /** Callback when AbilityButton3 is pressed. */
  UFUNCTION()
  void HandleAbilityButtonPressedSlot3();

  /** Update all stat text panels from the bound fighter. */
  void UpdateStatPanel();
  void UpdateAbilityButtons();
  void UpdateAbilityButtonForSlot(ESkaldAbilitySlot Slot, UButton *Button,
                                  UImage *IconWidget, UTextBlock *LabelWidget);
  const FBattleAbilitySlotDisplay *FindAbilityDisplay(ESkaldAbilitySlot Slot) const;

  /** Update ability slot widgets to match the bound fighter. */
  void RefreshAbilityDisplay();

  /** Update the visibility state of action buttons. */
  void UpdateActionButtonVisibility();

  /** Respond to health changes from the fighter. */
  UFUNCTION()
  void HandleHealthChanged(int32 NewHealth);

  /** Respond to action count changes from the fighter. */
  UFUNCTION()
  void HandleActionsChanged(int32 NewActions);

  UFUNCTION()
  void HandleAbilityComponentUpdated(USkaldAbilityComponent *AbilityComponent);

  /** Find the grid overlay component in the world. */
  UGridOverlayComponent *FindGridOverlay() const;

  /** Hide the dice roller image after the timer elapses. */
  void HideDiceRoller();

  /** Draws the numeric fallback dice face onto the render target. */
  UFUNCTION()
  void HandleDiceRenderTargetUpdate(class UCanvas *Canvas, int32 Width,
                                    int32 Height);

  /** Cached numeric value for the pending dice render target update. */
  int32 PendingDiceRenderValue = 0;

  /** Hide the initiative text after the timer elapses. */
  void HideInitiativeText();

  /** Reveal the initiative roll button after a short delay. */
  void RevealInitiativeRollButton();

  void ProcessNextDiceResolution();

  UFUNCTION()
  void HandleDicePanelResolved(const FDiceRollResult &Result);

  UFUNCTION()
  void HandleDiceOutcomeRevealed(const FDiceRollOutcome &Outcome,
                                 int32 RevealIndex);

  void BeginHealthTextHold(AFighterPawn *Fighter, int32 DisplayValue,
                           int32 FinalValue);

  void ReleaseHealthTextHold(AFighterPawn *Fighter);

  void ClearHealthTextHold();

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
  FSkaldFloaterStyle CriticalFloaterStyle;

  UPROPERTY(EditAnywhere, Category = "Skald|Battle|Floaters")
  FSkaldFloaterStyle HighStakesCriticalFloaterStyle;

  UPROPERTY(EditAnywhere, Category = "Skald|Battle|Floaters")
  TMap<ESkaldFaction, FSkaldFloaterStyle> HighStakesFloaterOverrides;

  /** Default colour for misses. */
  UPROPERTY(EditAnywhere, Category = "Skald|Battle|Floaters")
  FLinearColor MissFloaterColor = FLinearColor(0.7f, 0.7f, 0.74f);

  /** Default colour for health summary floaters. */
  UPROPERTY(EditAnywhere, Category = "Skald|Battle|Floaters")
  FLinearColor HealthFloaterColor = FLinearColor(0.82f, 0.82f, 0.9f);

  /** Offset applied above the fighter when spawning floaters. */
  UPROPERTY(EditAnywhere, Category = "Skald|Battle|Floaters")
  float FloaterAnchorHeightOffset = 60.f;

  /** Vertical spacing applied when stacking multiple floaters. */
  UPROPERTY(EditAnywhere, Category = "Skald|Battle|Floaters")
  float FloaterStackSpacing = 36.f;

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

  TWeakObjectPtr<AFighterPawn> HeldHealthTextFighter;
  bool bHealthTextHoldActive = false;
  bool bHasPendingHealthTextValue = false;
  int32 PendingHealthTextValue = 0;

  /** Whether movement preview is currently shown. */
  bool bMoveSelected = false;

  /** Whether attack preview is currently shown. */
  bool bAttackSelected = false;

  /** Whether action buttons are allowed to be displayed. */
  bool bActionButtonsUnlocked = false;

  /** Map of tracked fighters to their entry widgets. */
  TMap<TWeakObjectPtr<AFighterPawn>, TObjectPtr<ULockedInFighterEntryWidget>>
      LockedInFighterEntries;

  /** Ordered array of fighters displayed in the list. */
  TArray<TWeakObjectPtr<AFighterPawn>> LockedInFighterOrder;

  /** Currently highlighted fighter entry (selection). */
  TWeakObjectPtr<AFighterPawn> HighlightedLockedInFighter;

  /** Fighter currently taking its activation. */
  TWeakObjectPtr<AFighterPawn> ActiveLockedInFighter;

  /** Fighter currently bound to the HUD. */
  UPROPERTY()
  AFighterPawn *BoundFighter;

  /** Ability component currently providing passive/active data. */
  TWeakObjectPtr<USkaldAbilityComponent> BoundAbilityComponent;

  /** Timer managing dice roll visibility. */
  FTimerHandle DiceRollerHideTimer;

  /** Timer managing initiative label visibility. */
  FTimerHandle InitiativeHideTimer;

  /** Timer delaying the display of the initiative roll button. */
  FTimerHandle InitiativeRollButtonDelayTimer;

  void ApplyDiceResolutionPanelLayoutInternal(const FDiceResolutionPanelLayout &Layout);
};

