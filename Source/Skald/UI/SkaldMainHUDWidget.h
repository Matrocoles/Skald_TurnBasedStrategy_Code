#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "GridBattleManager.h"
#include "SkaldTypes.h"
#include "TimerManager.h"
#include "UI/W_DiceResolutionPanel.h"
#include "Containers/Set.h"
#include "SkaldMainHUDWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UVerticalBox;
class USkaldPlayerListEntryWidget;
class ATerritory;
class UConfirmAttackWidget;
class UPrepareForBattleWidget;
class UDeployWidget;
class UEngineeringWidget;
class UWidget;
class SWidget;
class ASkaldGameMode;
class ASkaldGameState;
class USkaldGameInstance;
class USoundBase;
class UCombatFloaterPoolSubsystem;
class UTexture2D;
class UW_FloatingText;
class UW_DiceResolutionPanel;
class AFighterPawn;
class USiegeTransferPromptWidget;

struct FSkaldActiveFloater {
  TWeakObjectPtr<UW_FloatingText> Floater;
  FVector AnchorLocation = FVector::ZeroVector;
  FVector2D InitialOffset = FVector2D::ZeroVector;
  float HorizontalDirection = 1.f;
  float Lifetime = 1.5f;
  float FadeDuration = 0.35f;
  float Elapsed = 0.f;
  float Scale = 1.f;
};

// Delegates broadcasting user UI actions to game logic
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FSkaldAttackRequested, int32,
                                              FromID, int32, ToID, int32,
                                              ArmySent, bool, bUseSiege);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSkaldBuildSiegeRequested, int32,
                                             TerritoryID, ESiegeWeapon,
                                             SiegeType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSkaldEndAttackRequested, bool,
                                            bConfirmed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSkaldEngineeringRequested, int32,
                                             CapitalID, EEngineeringAction,
                                             UpgradeType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSkaldDigTreasureRequested, int32,
                                            TerritoryID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FSkaldMoveRequested, int32,
                                              FromID, int32, ToID, int32,
                                              Troops, bool, bTransferSiege);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSkaldEndMovementRequested, bool,
                                            bConfirmed);

/**
 * Base HUD widget that exposes state and events for the game logic.
 *
 * Blueprint subclasses are expected to create the visual elements. Typical
 * wiring:
 *  - Buttons in the BP call the BlueprintCallable functions such as
 * SubmitAttack or SubmitMove. (e.g. AttackButton->OnClicked ->
 * SubmitAttack(SourceID, TargetID, ArmySent))
 *  - PlayerController binds to the multicast delegates to forward actions to
 * server RPCs: HUDWidget->OnAttackRequested.AddDynamic(this,
 * &ASKald_PlayerController::Server_RequestAttack);
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API USkaldMainHUDWidget : public UUserWidget {
  GENERATED_BODY()

  friend class UDeployWidget;
  friend class UEngineeringWidget;
  friend class USiegeTransferPromptWidget;

public:
  USkaldMainHUDWidget(const FObjectInitializer& ObjectInitializer);

  virtual void NativeTick(const FGeometry& MyGeometry,
                          float InDeltaTime) override;

  // Identity / state (read by BP)
  UPROPERTY(BlueprintReadWrite, Category = "Skald|State")
  int32 LocalPlayerID = -1;

  UPROPERTY(BlueprintReadWrite, Category = "Skald|State")
  int32 CurrentPlayerID = -1;

  UPROPERTY(BlueprintReadWrite, Category = "Skald|State")
  int32 TurnNumber = 1;

  UPROPERTY(BlueprintReadWrite, Category = "Skald|State")
  ETurnPhase CurrentPhase = ETurnPhase::Reinforcement;

  /** Whether the HUD is waiting on the strategic initiative roll. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|State")
  bool bAwaitingStrategicInitiative = false;

  // Selection helpers used by Attack/Move flows
  UPROPERTY(BlueprintReadWrite, Category = "Skald|Selection")
  int32 SelectedSourceID = -1;

  UPROPERTY(BlueprintReadWrite, Category = "Skald|Selection")
  int32 SelectedTargetID = -1;

  UPROPERTY(BlueprintReadWrite, Category = "Skald|Selection")
  bool bSelectingForAttack = false;

  UPROPERTY(BlueprintReadWrite, Category = "Skald|Selection")
  bool bSelectingForMove = false;

  UPROPERTY(BlueprintReadWrite, Category = "Skald|Engineering")
  bool bSelectingForEngineering = false;

  /** Sound played whenever a new world round begins. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Audio")
  USoundBase *RoundStartSound = nullptr;

  UPROPERTY(BlueprintReadWrite, Category = "Skald|Siege")
  bool bUseSiegeForNextAttack = false;

  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skald|Engineering")
  TSubclassOf<UEngineeringWidget> EngineeringWidgetClass;

  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skald|Siege")
  TSubclassOf<USiegeTransferPromptWidget> SiegeTransferPromptClass;

  // Cached list of players for UI list building
  UPROPERTY(BlueprintReadWrite, Category = "Skald|Data")
  TArray<FS_PlayerData> CachedPlayers;

  // References to core game objects for blueprint access
  UPROPERTY(BlueprintReadOnly, Category = "Skald|State")
  ASkaldGameMode* GameMode = nullptr;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|State")
  ASkaldGameState* GameState = nullptr;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|State")
  USkaldGameInstance* GameInstance = nullptr;

  // Delegates (BlueprintAssignable) — UI → game actions
  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldAttackRequested OnAttackRequested;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldEndAttackRequested OnEndAttackRequested;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldEngineeringRequested OnEngineeringRequested;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldBuildSiegeRequested OnBuildSiegeRequested;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldDigTreasureRequested OnDigTreasureRequested;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldMoveRequested OnMoveRequested;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldEndMovementRequested OnEndMovementRequested;

  /** Delegate fired when the local player marks themselves as ready. */
  DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSkaldPrepareForBattleReady);
  DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSkaldRetreatRequested);
  DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSkaldRetreatDestinationChosen,
                                              int32, TerritoryID);
  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldPrepareForBattleReady OnPrepareForBattleReady;
  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldRetreatRequested OnRetreatRequested;
  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldRetreatDestinationChosen OnRetreatDestinationChosen;

  /** Delegate fired when the strategic initiative roll button is pressed. */
  DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStrategicInitiativeRollRequested);
  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FOnStrategicInitiativeRollRequested OnStrategicInitiativeRollRequested;

  DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
      FOnDiceResolutionComplete, AFighterPawn *, Attacker, AFighterPawn *,
      Defender, const FDiceRollResult &, Result);
  DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
      FOnDiceOutcomeRevealed, AFighterPawn *, Attacker, AFighterPawn *, Defender,
      const FDiceRollOutcome &, Outcome, int32, RevealIndex);
  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Events")
  FOnDiceResolutionComplete OnResolutionComplete;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Events")
  FOnDiceOutcomeRevealed OnDiceOutcomeRevealed;

  // BlueprintCallable functions — game → HUD (push updates)
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void UpdateTurnBanner(int32 InCurrentPlayerID, int32 InTurnNumber);

  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void UpdatePhaseBanner(ETurnPhase InPhase);

  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void UpdateTerritoryInfo(const FString &TerritoryName,
                           const FString &OwnerName, int32 ArmyCount);

  /** Hide the territory info panel when no selection is active. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void ClearTerritoryInfo();

  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void RefreshPlayerList(const TArray<FS_PlayerData> &Players);

  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void RefreshFromState(int32 InCurrentPlayerID, int32 InTurnNumber,
                        ETurnPhase InPhase,
                        const TArray<FS_PlayerData> &Players);

  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void ShowTurnAnnouncement(const FString &PlayerName);

  /** Rebuilds the cached player list into PlayerListBox. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void RebuildPlayerList(const TArray<FS_PlayerData> &Players);

  /** Show a message indicating the turn is ending. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void ShowEndingTurn();

  /** Hide the ending turn message. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void HideEndingTurn();

  /** Show whose turn it is and toggle the End Turn button. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void ShowTurnMessage(bool bIsMyTurn);

  /** Display the prompt instructing the player to roll for strategic initiative. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD|Initiative")
  void ShowStrategicInitiativePrompt(const FText &PromptText, float ButtonDelay = 1.f);

  /** Hide the strategic initiative roll prompt and associated button. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD|Initiative")
  void HideStrategicInitiativePrompt();

  /** Display the prepare for battle confirmation widget. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD|Battle")
  void ShowPrepareForBattleDialog(
      const FPrepareForBattlePromptData &PromptData);

  /** Hide the prepare for battle widget if active. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD|Battle")
  void HidePrepareForBattleDialog();

  /** Begin selecting a territory to retreat into. */
  void BeginRetreatSelection(int32 DefendingTerritoryID,
                             const TArray<int32> &CandidateTerritoryIDs);

  /** Clear retreat selection state and highlights. */
  void CompleteRetreatSelection();

  /** Display a retreat-specific error message. */
  void ShowRetreatUnavailableMessage(const FText &Message);

  /** Notify the player that the enemy retreated from battle. */
  /**
   * Notify the HUD that the opposing force successfully retreated.
   *
   * Returns true when the active prepare-for-battle widget displayed the
   * retreat status message, allowing callers to defer tearing it down until
   * after the message has been visible for a short duration.
   */
  bool ShowEnemyRetreatedMessage();

  /** Display the rolled initiative value using the configured dice visuals. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD|Initiative")
  void ShowStrategicInitiativeRoll(int32 RollValue, float DisplayDuration = 1.f);

  /** Toggle the HUD state while awaiting the strategic initiative roll. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD|Initiative")
  void SetAwaitingStrategicInitiative(bool bAwaiting);

  /** Show an in-progress enemy turn message without auto-hiding. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void ShowEnemyTurnInProgress(const FString &Message);

  /** Hide the in-progress enemy turn message. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void HideEnemyTurnInProgress();

  /** Display a message that a player has ended their turn. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void ShowTurnEnded(const FString &PlayerName);

  /** Queue a dice resolution sequence for the shared dice panel. */
  void QueueDiceResolution(AFighterPawn *Attacker, AFighterPawn *Defender,
                           const FDiceRollResult &Result,
                           bool bManualReveal = false);

  /** Override the default layout values at runtime. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Widgets|Dice")
  void SetDefaultDiceResolutionPanelLayout(const FDiceResolutionPanelLayout &Layout);

  /** Apply a layout override immediately without mutating the defaults. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Widgets|Dice")
  void ApplyDiceResolutionPanelLayout(const FDiceResolutionPanelLayout &Layout);

  /** Blueprint hook for computing layout on a per-resolution basis. */
  UFUNCTION(BlueprintNativeEvent, Category = "Skald|Widgets|Dice")
  FDiceResolutionPanelLayout ResolveDiceResolutionPanelLayout(
      AFighterPawn *Attacker, AFighterPawn *Defender, const FDiceRollResult &Result) const;
  FDiceResolutionPanelLayout ResolveDiceResolutionPanelLayout_Implementation(
      AFighterPawn *Attacker, AFighterPawn *Defender, const FDiceRollResult &Result) const;

  /** Update and display the initiative announcement. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void UpdateInitiativeText(const FString &Message);

  /** Update the remaining deployable unit count display. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  virtual void UpdateDeployableUnits(int32 UnitsRemaining);

  /** Update the player's visible gold total. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void UpdateGold(int32 GoldAmount);

  /** Retrieve the gold cost for the requested siege weapon. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Engineering")
  int32 GetSiegeGoldCost(ESiegeWeapon SiegeType) const;

  /** Display an error message to the player. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  virtual void ShowErrorMessage(const FString &Message);

  /** Display a transient status message centered on the HUD. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void ShowStatusMessage(const FText &Message, float DisplayDuration);

  /** Hide the active status message if one is visible. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void HideStatusMessage();

  /** Display a temporary warning about the army placement limit. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void ShowArmyPlacementLimitWarning(const FText &Message);

  /** Cancel an in-progress deployment selection and hide related UI. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void HandleDeploymentCancelled();

  /** Blueprint hook to draw the error message. */
  UFUNCTION(BlueprintImplementableEvent, Category = "Skald|HUD")
  void BP_ShowErrorMessage(const FString &Message);

  /** Blueprint hook for showing the global status message. */
  UFUNCTION(BlueprintImplementableEvent, Category = "Skald|HUD")
  void BP_ShowStatusMessage(const FText &Message, float DisplayDuration);

  /** Blueprint hook for hiding the global status message. */
  UFUNCTION(BlueprintImplementableEvent, Category = "Skald|HUD")
  void BP_HideStatusMessage();

  /** Display floating combat text anchored to a world position. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD|Floaters")
  void ShowFloatingTextAtLocation(const FVector &WorldLocation,
                                  const FText &Message,
                                  const FLinearColor &Tint,
                                  float Scale = 1.f,
                                  float LifetimeOverride = -1.f);

  // BlueprintCallable functions — selection UX helpers
  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void BeginAttackSelection();

  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void SubmitAttack(int32 FromID, int32 ToID, int32 ArmySent,
                    bool bUseSiege);

  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void CancelAttackSelection();

  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void BeginMoveSelection();

  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void SubmitMove(int32 FromID, int32 ToID, int32 Troops,
                  bool bTransferSiege);

  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void CancelMoveSelection();

  /** Display the outcome of a move attempt originating from this HUD. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void HandleMoveOutcome(bool bSuccess, const FString &Message);

  /** Reset movement selection after a failed attempt so players can retry. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void ResetMoveSelectionAfterInvalidAttempt();

  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void OnTerritoryClickedUI(ATerritory *Territory);

  UFUNCTION(BlueprintCallable, Category = "Skald|Engineering")
  void BeginEngineeringSelection();

  UFUNCTION(BlueprintCallable, Category = "Skald|Engineering")
  void CancelEngineeringSelection();

  UFUNCTION(BlueprintCallable, Category = "Skald|Engineering")
  void SubmitEngineeringAction(int32 CapitalID, EEngineeringAction Action);

  UFUNCTION(BlueprintCallable, Category = "Skald|Movement")
  void HandleMoveAmountChosen(int32 FromID, int32 ToID, int32 Troops);

  UFUNCTION(BlueprintCallable, Category = "Skald|Siege")
  void BuildSiege(int32 TerritoryID, ESiegeWeapon SiegeType);

  UFUNCTION(BlueprintCallable, Category = "Skald|Siege")
  void SetUseSiegeForNextAttack(bool bEnable);

  // BlueprintImplementableEvent hooks — BP subclass draws UI
  UFUNCTION(BlueprintImplementableEvent, Category = "Skald|HUD")
  void BP_SetTurnText(int32 InTurnNumber, int32 InCurrentPlayerID);

  UFUNCTION(BlueprintImplementableEvent, Category = "Skald|HUD")
  void BP_SetPhaseText(ETurnPhase InPhase);

  UFUNCTION(BlueprintImplementableEvent, Category = "Skald|HUD")
  void BP_SetTerritoryPanel(const FString &TerritoryName,
                            const FString &OwnerName, int32 ArmyCount);

  UFUNCTION(BlueprintImplementableEvent, Category = "Skald|HUD")
  void BP_ClearTerritoryPanel();

  UFUNCTION(BlueprintImplementableEvent, Category = "Skald|HUD")
  void BP_SetPhaseButtons(ETurnPhase InPhase, bool bIsMyTurn);

  UFUNCTION(BlueprintImplementableEvent, Category = "Skald|HUD")
  void BP_ShowTurnAnnouncement(const FString &PlayerName);

  // Helper so PlayerController can refresh button enable state after it knows
  // turn ownership
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void SyncPhaseButtons(bool bIsMyTurn);

public:
  // Bound widget references - optional so subclasses can customise layouts
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidget))
  UTextBlock *TurnText;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidget))
  UTextBlock *PhaseText;

  /** Legacy selection prompt text block kept for backward compatibility. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidget))
  UTextBlock *SelectionPrompt;

  /** Optional dedicated text block for selection prompts. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UTextBlock *SelectionPromptText;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|HUD|Prompts")
  FText ReinforcementSelectionPromptText;

  /**
   * Optional overrides for selection prompts/errors keyed by LOCTEXT IDs.
   *
   * Supported keys include ReinforcementOwnedCapitalPrompt, AttackPrompt,
   * MovementPrompt, MoveInvalidSelection, etc. (See SkaldMainHUDWidget.cpp for
   * the complete list.)
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|HUD|Prompts")
  TMap<FName, FText> SelectionPromptOverrides;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidget))
  UButton *AttackButton;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidget))
  UButton *MoveButton;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidget))
  UButton *EndTurnButton;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidget))
  UButton *EndPhaseButton;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidget))
  UButton *DeployButton;

  // Container where RebuildPlayerList will spawn entries
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidget))
  UVerticalBox *PlayerListBox;

  /** Optional widget class used when spawning player list entries. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Widgets")
  TSubclassOf<USkaldPlayerListEntryWidget> PlayerListEntryWidgetClass;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidget))
  UTextBlock *EndingTurnText;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidget))
  UTextBlock *InitiativeText;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *RollInitiativeButton;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UTextBlock *InitiativePromptText;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UImage *InitiativeDiceImage;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UImage *InitiativeDiceBoardImage;

  /** Background image that mirrors the territory panel visibility. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UImage *TerritoryPanelpng;

  /** Container holding the territory name/owner/army details. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UWidget *TerritoryInfoPanel;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidget))
  UTextBlock *DeployableUnitsText;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UTextBlock *GoldText = nullptr;

  /** Legacy binding retained for widgets that still expose a ResourcesText field. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UTextBlock *ResourcesText = nullptr;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UW_DiceResolutionPanel *DiceResolutionPanel;

  /** Dice face textures that should be mirrored on the resolution panel. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Widgets|Dice")
  TArray<TObjectPtr<UTexture2D>> DiceFaceTextures;

  /** Default layout overrides applied to the strategic HUD dice panel. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Widgets|Dice")
  FDiceResolutionPanelLayout DefaultDiceResolutionPanelLayout;

  /** Optional sound played when the strategic initiative die is revealed. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Widgets|Dice")
  TObjectPtr<USoundBase> InitiativeDiceSound;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Widgets")
  TSubclassOf<UConfirmAttackWidget> ConfirmAttackWidgetClass;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Widgets")
  TSubclassOf<UPrepareForBattleWidget> PrepareForBattleWidgetClass;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Widgets")
  TSubclassOf<UDeployWidget> DeployWidgetClass;

protected:
  /** Ensure the broadcast text widget is positioned and styled for readability. */
  void ConfigureBroadcastText();

  /** Apply the appropriate color for player/enemy broadcast messages. */
  void ApplyBroadcastStyle(bool bIsPlayerMessage);
  const FS_PlayerData *FindPlayerDataById(int32 PlayerId) const;
  FLinearColor ResolveFactionColor(ESkaldFaction Faction) const;
  FLinearColor ResolvePlayerColor(int32 PlayerId) const;
  FLinearColor ResolveLocalPlayerColor() const;

  UFUNCTION()
  void HideInitiativeText();

  FTimerHandle InitiativeTimerHandle;
  FTimerHandle TurnMessageTimerHandle;
  FTimerHandle RetreatPromptTeardownHandle;
  FTimerHandle StrategicInitiativeRollDelayHandle;
  FTimerHandle StrategicInitiativeDiceHideHandle;
  FTimerHandle ArmyPlacementWarningTimerHandle;
  FTimerHandle StatusMessageTimerHandle;
  FText PendingSelectionPromptText;
  bool bPendingSelectionPromptVisible = false;
  FText CachedSelectionPromptText;
  bool bCachedSelectionPromptVisible = false;
  bool bArmyPlacementWarningActive = false;
  FText CachedStatusMessage;
  float CachedStatusMessageDuration = 0.f;
  bool bStatusMessageVisible = false;

  // Internal handlers for widget actions
  UFUNCTION()
  void HandleEndTurnClicked();

  UFUNCTION()
  void HandleEndPhaseClicked();

  UFUNCTION()
  void HandleDeployClicked();

  UFUNCTION()
  void HandleStrategicInitiativeRollPressed();

  void RevealStrategicInitiativeRollButton();
  void HideStrategicInitiativeDice();
  void ClearStrategicInitiativeWaitIfNeeded();
  bool IsStrategicInitiativeOverlayActive() const;
  void ApplyPendingSelectionPrompt();
  UTextBlock *GetSelectionPromptTextBlock() const;
  void HandleArmyPlacementWarningExpired();

  UFUNCTION()
  void HandleStrategicDiceRenderTargetUpdate(class UCanvas *Canvas, int32 Width,
                                             int32 Height);

  void UpdateActiveFloaters(float DeltaSeconds);
  void ReleaseFloaterAtIndex(int32 Index);
  UCombatFloaterPoolSubsystem *ResolveFloaterPool();
  void ProcessNextDiceResolution();

  UFUNCTION()
  void HandleDicePanelResolved(const FDiceRollResult &Result);

  UFUNCTION()
  void HandleDiceOutcomeRevealed(const FDiceRollOutcome &Outcome,
                                 int32 RevealIndex);

  UFUNCTION()
  void HandleAttackApproved();

  UFUNCTION()
  void HandlePrepareForBattleClicked();

  UFUNCTION()
  void HandleRetreatClicked();

  void ClearDeployWidget();
  void ClearEngineeringWidget();
  bool ValidateEngineeringActionRequest(int32 CapitalID,
                                        FString &OutError) const;
  void PromptSiegeTransfer();
  void ResolveSiegeTransferChoice(bool bBringSiege);
  void BeginSiegePlacement(int32 CapitalID);
  void HandleSiegePlacementTarget(ATerritory *Territory);
  bool DoesTerritoryContainSiege(int32 TerritoryID) const;

  UPROPERTY()
  UConfirmAttackWidget *ActiveConfirmWidget = nullptr;

  UPROPERTY()
  UPrepareForBattleWidget *ActivePrepareForBattleWidget = nullptr;

  bool bPrepareForBattleReadySent = false;
  bool bRetreatRequestPending = false;
  bool bSelectingRetreatDestination = false;
  bool bAwaitingRetreatConfirmation = false;
  int32 RetreatDefendingTerritoryID = -1;
  TSet<int32> RetreatCandidateIds;
  FPrepareForBattlePromptData ActivePreparePrompt;
  bool bHasActivePreparePrompt = false;
  bool bLocalPlayerIsDefender = false;

  /** Class used when requesting floaters from the subsystem. */
  UPROPERTY(EditAnywhere, Category = "Skald|HUD|Floaters")
  TSubclassOf<UW_FloatingText> FloaterWidgetClass;

  /** How long floaters remain visible in seconds. */
  UPROPERTY(EditAnywhere, Category = "Skald|HUD|Floaters")
  float FloaterLifetime = 1.6f;

  /** Portion of the lifetime reserved for fading out. */
  UPROPERTY(EditAnywhere, Category = "Skald|HUD|Floaters")
  float FloaterFadeDuration = 0.35f;

  /** Maximum vertical offset (in pixels) applied across the arc. */
  UPROPERTY(EditAnywhere, Category = "Skald|HUD|Floaters")
  float FloaterArcHeight = 120.f;

  void ApplyDiceResolutionPanelLayoutInternal(const FDiceResolutionPanelLayout &Layout);

  /** Horizontal drift (in pixels) applied over the lifetime. */
  UPROPERTY(EditAnywhere, Category = "Skald|HUD|Floaters")
  float FloaterHorizontalDrift = 40.f;

  /** Margin used when clamping to the screen bounds. */
  UPROPERTY(EditAnywhere, Category = "Skald|HUD|Floaters")
  float FloaterClampMargin = 24.f;

  /** Active floating text widgets driven by the HUD tick. */
  TArray<FSkaldActiveFloater> ActiveFloaters;

  TWeakObjectPtr<UCombatFloaterPoolSubsystem> CachedFloaterPool;

  struct FQueuedDiceResolution {
    TWeakObjectPtr<AFighterPawn> Attacker;
    TWeakObjectPtr<AFighterPawn> Defender;
    FDiceRollResult Result;
  };

  TArray<FQueuedDiceResolution> PendingDiceResolutions;
  bool bDiceResolutionActive = false;
  FQueuedDiceResolution ActiveDiceResolution;

  UPROPERTY(Transient)
  TObjectPtr<class UCanvasRenderTarget2D> StrategicInitiativeDiceRenderTarget;

  int32 PendingStrategicInitiativeValue = 0;

  UPROPERTY()
  UDeployWidget *ActiveDeployWidget = nullptr;

  UPROPERTY()
  UEngineeringWidget *ActiveEngineeringWidget = nullptr;

  UPROPERTY()
  USiegeTransferPromptWidget *ActiveSiegeTransferPrompt = nullptr;

  UPROPERTY()
  int32 SelectedEngineeringCapitalID = -1;

  UPROPERTY()
  bool bSelectingSiegeDestination = false;

  UPROPERTY()
  int32 PendingSiegeCapitalID = -1;

  UPROPERTY()
  TArray<int32> PendingSiegeDestinationIDs;

  UPROPERTY()
  ESiegeWeapon PendingSiegeType = ESiegeWeapon::BatteringRam;

  UPROPERTY()
  int32 PendingMoveFromID = -1;

  UPROPERTY()
  int32 PendingMoveToID = -1;

  UPROPERTY()
  int32 PendingMoveTroops = 0;

  /** Prevent reconfiguring the broadcast text multiple times. */
  bool bBroadcastTextConfigured = false;

  UPROPERTY()
  TArray<ATerritory *> HighlightedTerritories;

  void ClearTerritoryHighlights();
  void ShowSelectionPromptMessage(const FText &Message, bool bShow = true);
  void ShowSelectionErrorMessage(const FText &Message);
  FText ResolveSelectionPromptText(const FName &Key, const FText &Default) const;

  ATerritory *GetCurrentlySelectedTerritory() const;

  virtual void NativeConstruct() override;
  virtual void NativeDestruct() override;

  /** Refresh player list when the game state notifies us of a change. */
  UFUNCTION()
  void HandlePlayersUpdated();

  /** GS -> HUD: react to turn index changes (client + server). */
  UFUNCTION()
  void HandleTurnIndexChanged(int32 NewTurnIndex);

  int32 ResolveLocalPlayerId() const;
};
