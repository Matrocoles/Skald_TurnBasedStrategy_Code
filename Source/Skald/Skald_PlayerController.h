#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/PlayerController.h"
#include "GridBattleManager.h"
#include "SkaldTypes.h"
#include "TimerManager.h"
#include "Delegates/Delegate.h"
#include "Camera/CameraShakeBase.h"
#include "UObject/WeakObjectPtr.h"
#include "Skald_PlayerController.generated.h"

class ATurnManager;
class UUserWidget;
class USkaldMainHUDWidget;
#if defined(WITH_AUTOMATION_TESTS) && WITH_AUTOMATION_TESTS
class ATestPlayerController;
#endif
class UChoosePlayerWidget;
class UBattleHUDWidget;
class UInGameMenuWidget;
class ATerritory;
class ASkaldGameMode;
class ASkaldGameState;
class USkaldGameInstance;
class UFighterSelectionWidget;
class AWorldMap;
class AFighterPawn;
class AActor;
class UGridOverlayComponent;
class UWorld;
class ASkald_BattleGameMode;
class USoundBase;
class UCameraShakeBase;
class UNiagaraSystem;
class ASkald_PlayerCharacter;

/** Command issued by the player during a battle. */
UENUM()
enum class EBattleCommandMode : uint8 { None, Move, Attack };

/**
 * Player controller capable of participating in turn based gameplay.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ASkaldPlayerController : public APlayerController {
  GENERATED_BODY()

  /** Allow the game mode to trigger binding once the world map exists. */
  friend class ASkaldGameMode;
  /** Allow the player state to notify us when its player ID changes. */
  friend class ASkaldPlayerState;
#if defined(WITH_AUTOMATION_TESTS) && WITH_AUTOMATION_TESTS
  /** Permit the automation test subclass to access validation helpers. */
  friend class ATestPlayerController;
#endif

public:
  ASkaldPlayerController();

  virtual void OnRep_PlayerState() override;

  virtual void OnPossess(APawn *InPawn) override;

  virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  UFUNCTION(BlueprintCallable, Category = "Turn")
  virtual void StartTurn();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  virtual void EndTurn();

  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn")
  bool IsMyTurn() const;

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void EndPhase();

  /** Request the server to end the current phase for this controller. */
  UFUNCTION(Server, Reliable)
  void ServerEndPhase();

  /** Set the turn manager responsible for sequencing play. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void SetTurnManager(ATurnManager *Manager);

  /** RepNotify hook for TurnManager replication. */
  UFUNCTION()
  void OnRep_TurnManager();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void ShowTurnAnnouncement(const FString &PlayerName, bool bIsMyTurn);

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void NotifyTurnEnded(const FString &PlayerName);

  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleTerritorySelected(ATerritory *Terr);

  /** Accessor for the main HUD widget instance. */
  UFUNCTION(BlueprintCallable, Category = "UI")
  USkaldMainHUDWidget *GetHUDWidget() const { return MainHUD; }

  UFUNCTION(BlueprintCallable)
  void ShowMainHUD();

  UFUNCTION(BlueprintCallable)
  void HideMainHUD();

  UFUNCTION(BlueprintCallable, Category = "UI")
  void ToggleInGameMenu();

  UFUNCTION(BlueprintCallable, Category = "UI")
  void ShowInGameMenu();

  UFUNCTION(BlueprintCallable, Category = "UI")
  void HideInGameMenu();

  /** Retrieve the turn manager controlling this player. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn")
  ATurnManager *GetTurnManager() const { return TurnManager; }

  /** Send the local player's initial data to the server for replication. */
  UFUNCTION(Server, Reliable)
  void ServerInitPlayerState(const FString &Name, ESkaldFaction Faction,
                             int32 NumAIPlayers);

  UFUNCTION(Client, Reliable)
  void Client_ShowFighterSelection(int32 MaxBudget, ESkaldFaction Faction);

  UFUNCTION(Server, Reliable, WithValidation)
  void Server_CommitArmy(const TArray<FFighterDefinition> &Chosen);

  UFUNCTION(Server, Reliable)
  void Server_LockInSelection(const TArray<FFighterDefinition> &SelectedFighters);

  UFUNCTION(Client, Reliable)
  void Client_OnLockInResult(bool bSuccess, const FString &Reason);

  UFUNCTION()
  void HandleBattlePhaseChanged();

protected:
  /** Widget class to instantiate for the player's HUD.
   *  Settable via Blueprint or loaded in the constructor. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
  TSubclassOf<USkaldMainHUDWidget> MainHUDClass;

  /** Widget class used for in-battle HUD. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
  TSubclassOf<UBattleHUDWidget> BattleHUDWidgetClass;

  /** Widget class providing the in-game menu overlay. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
  TSubclassOf<UInGameMenuWidget> InGameMenuWidgetClass;

  /** Widget class used for the fighter selection flow before battles. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
  TSubclassOf<UFighterSelectionWidget> FighterSelectionWidgetClass;

  /** Widget class used for the player faction selection screen. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
  TSubclassOf<UChoosePlayerWidget> ChoosePlayerWidgetClass;

  /** Widget class used for displaying battle victory. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
  TSubclassOf<UUserWidget> VictoryWidgetClass;

  /** Sound to play for the local player when an initiative prompt appears. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Audio")
  USoundBase *BattleRoundStartSound = nullptr;

  /** Sound to play for the local player when their world map turn begins. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Audio")
  USoundBase *WorldTurnStartSound = nullptr;

  /** Sound to play for the local player when their grid battle turn begins. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Audio")
  USoundBase *BattleTurnStartSound = nullptr;

  /** Sound to play when the local player wins the initiative roll. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Audio")
  USoundBase *InitiativeWinSound = nullptr;

  /** Camera shake to trigger for successful hits. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  TSubclassOf<UCameraShakeBase> HitCameraShakeClass;

  /** Camera shake to trigger for misses or glancing blows. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  TSubclassOf<UCameraShakeBase> MissCameraShakeClass;

  /** Niagara or particle cue to spawn for successful hits. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  UNiagaraSystem *HitImpactEffect = nullptr;

  /** Niagara or particle cue to spawn for misses. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  UNiagaraSystem *MissImpactEffect = nullptr;

  /** Default Niagara effect triggered by a high-stakes critical. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  UNiagaraSystem *DefaultHighStakesCriticalEffect = nullptr;

  /** Optional faction overrides for high-stakes critical effects. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  TMap<ESkaldFaction, UNiagaraSystem *> HighStakesCriticalFactionEffects;

  /** Default audio cue triggered by a high-stakes critical. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  USoundBase *DefaultHighStakesCriticalSound = nullptr;

  /** Optional faction overrides for high-stakes critical audio. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  TMap<ESkaldFaction, USoundBase *> HighStakesCriticalFactionSounds;

  /** Default audio cue triggered when a die shows a natural six. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  USoundBase *DefaultNaturalSixSound = nullptr;

  /** Optional faction overrides for natural six audio. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  TMap<ESkaldFaction, USoundBase *> NaturalSixFactionSounds;

  /** Default Niagara effect triggered when a die shows a natural six. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  UNiagaraSystem *DefaultNaturalSixEffect = nullptr;

  /** Optional faction overrides for natural six effects. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  TMap<ESkaldFaction, UNiagaraSystem *> NaturalSixFactionEffects;

  /** Default decal-style Niagara effect for natural six results. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  UNiagaraSystem *DefaultNaturalSixDecalEffect = nullptr;

  /** Optional faction overrides for natural six decal effects. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  TMap<ESkaldFaction, UNiagaraSystem *> NaturalSixDecalFactionEffects;

  /** Lifetime of the spawned natural six decal Niagara system. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  float NaturalSixDecalLifetimeSeconds = 0.f;

  /** Scale applied to spawned natural six decal Niagara systems. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  FVector NaturalSixDecalScale = FVector::OneVector;

  /** Vertical offset applied to natural six decal Niagara systems. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  float NaturalSixDecalHeightOffset = 5.f;

  /** Default Niagara effect triggered when a fighter dies. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  UNiagaraSystem *DefaultFighterDeathEffect = nullptr;

  /** Optional faction overrides for fighter death effects. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  TMap<ESkaldFaction, UNiagaraSystem *> FighterDeathFactionEffects;

  /** Default splatter Niagara effect spawned after a fighter dies. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  UNiagaraSystem *DefaultFighterDeathSplatterEffect = nullptr;

  /** Optional faction overrides for fighter death splatter effects. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  TMap<ESkaldFaction, UNiagaraSystem *> FighterDeathSplatterFactionEffects;

  /** Lifetime applied to spawned fighter death splatter Niagara systems. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  float FighterDeathSplatterLifetimeSeconds = 4.5f;

  /** Scale applied to spawned fighter death splatter Niagara systems. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  FVector FighterDeathSplatterScale = FVector::OneVector;

  /** Vertical offset applied to fighter death splatter Niagara systems. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  float FighterDeathSplatterHeightOffset = 5.f;

  /** Audio cue played when a hit lands. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  USoundBase *HitImpactSound = nullptr;

  /** Default decal-style Niagara effect triggered when a hit lands. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  UNiagaraSystem *DefaultHitDecalEffect = nullptr;

  /** Optional faction overrides for hit decal Niagara effects. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  TMap<ESkaldFaction, UNiagaraSystem *> HitDecalFactionEffects;

  /** Lifetime applied to spawned hit decal Niagara systems. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  float HitDecalLifetimeSeconds = 0.f;

  /** Scale applied to spawned hit decal Niagara systems. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  FVector HitDecalScale = FVector::OneVector;

  /** Vertical offset applied to hit decal Niagara systems. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  float HitDecalHeightOffset = 5.f;

  /** Audio cue played when an attack misses. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle|Feedback")
  USoundBase *MissImpactSound = nullptr;

  /** Reference to the HUD widget instance. */
  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UUserWidget> HUDRef;

  /** Typed reference to the main HUD widget. */
  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<USkaldMainHUDWidget> MainHUD;

  /** Typed reference to the battle HUD widget. */
  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UBattleHUDWidget> BattleHudWidget;

  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UInGameMenuWidget> InGameMenuWidget;

  /** Battle result widget displayed after combat resolves. */
  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  UUserWidget *BattleResultWidget;

  /** Fighter selection widget used during battle setup. */
  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UFighterSelectionWidget> FighterSelectionWidget;

  /** Player selection widget instance. */
  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UChoosePlayerWidget> ChoosePlayerWidget;

  /** Cached references to core game singletons for blueprint access */
  UPROPERTY(BlueprintReadOnly, Category = "Game",
            meta = (AllowPrivateAccess = "true"))
  ASkaldGameMode *CachedGameMode;

  UPROPERTY(BlueprintReadOnly, Category = "Game",
            meta = (AllowPrivateAccess = "true"))
  ASkaldGameState *CachedGameState;

  UPROPERTY(BlueprintReadOnly, Category = "Game",
            meta = (AllowPrivateAccess = "true"))
  USkaldGameInstance *CachedGameInstance;

  /** Current command selection when issuing grid battle orders. */
  EBattleCommandMode CurrentCommandMode;

  /** Ensures ServerInitPlayerState and lock-in handling run only once. */
  bool bHasInitialized;

  /** Default mouse capture behavior for this controller. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
  EMouseCaptureMode DefaultMouseCaptureMode;

  virtual void BeginPlay() override;

  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  virtual void SetupInputComponent() override;

  UFUNCTION()
  void HandleWorldBeginPlay(UWorld *LoadedWorld);

  /** Shared implementation for ending the current phase on the server. */
  void HandleEndPhaseInternal();

  FDelegateHandle PostWorldBeginPlayHandle;

  /** Begin selecting a move destination. */
  UFUNCTION()
  void BeginMoveMode();

  /** Begin selecting an attack target. */
  UFUNCTION()
  void BeginAttackMode();

  /** Handle the player clicking on the grid. */
  UFUNCTION()
  void HandleGridClick();

  /** Find a fighter occupying the specified grid cell. */
  AFighterPawn *FindFighterAtCell(const FIntPoint &Cell) const;

  UFUNCTION()
  void HandleActivatePressed();

  UFUNCTION()
  void HandleEndTurnPressed();

  /** Respond when our PlayerState receives an updated PlayerId. */
  void HandlePlayerIdUpdated();

  UFUNCTION()
  void HandleRightClick();

public:
  /** Handle HUD attack submissions.
   *  Bound to USkaldMainHUDWidget::OnAttackRequested in the HUD.
   *  Blueprint widgets invoke this when an attack is submitted.
   */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleAttackRequested(int32 FromID, int32 ToID, int32 ArmySent,
                             bool bUseSiege);

  /** Handle HUD confirmation that the player is ready for battle. */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandlePrepareForBattleReady();

  /** Handle HUD move submissions.
   *  Bound to USkaldMainHUDWidget::OnMoveRequested in the HUD.
   *  Called when a move action is confirmed from a widget.
   */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleMoveRequested(int32 FromID, int32 ToID, int32 Troops);

  /** Handle HUD end-attack confirmations.
   *  Bound to USkaldMainHUDWidget::OnEndAttackRequested.
   *  Widgets call this after the player finishes attacking.
   */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleEndAttackRequested(bool bConfirmed);

  /** Handle HUD end-movement confirmations.
   *  Bound to USkaldMainHUDWidget::OnEndMovementRequested.
   *  Invoked when the HUD signals the end of movement phase.
   */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleEndMovementRequested(bool bConfirmed);

  /** Handle HUD engineering action requests. */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleEngineeringRequested(int32 CapitalID, uint8 UpgradeType);

  /** Handle HUD treasure digging requests. */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleDigTreasureRequested(int32 TerritoryID);

  /** React to player list changes in the game state. */
  UFUNCTION()
  void HandlePlayersUpdated();

  /** React to faction selections in the game instance. */
  UFUNCTION()
  void HandleFactionsUpdated();

  /** React to the game entering or exiting the streamed battle map. */
  UFUNCTION()
  virtual void HandleBattleMapStateChanged(bool bInBattleMap);

  /** React to world state changes broadcast by the turn manager. */
  UFUNCTION()
  void HandleWorldStateChanged();

  /**
   * Legacy alias for HandleFactionLockedIn.
   * Enables player movement and look input after locking in their faction.
   */
  UFUNCTION()
  void HandlePlayerLockedIn();

  /** React to the player finishing their pre-game selection. */
  UFUNCTION()
  void HandleFactionLockedIn();

  void UpdateBattleCameraMode();

  void HandleFighterSelectionLockedIn();

  /** React to the end of a battle. */
  UFUNCTION()
  virtual void HandleBattleEnded(ESkaldFaction WinningFaction,
                                 int32 AttackerCasualties,
                                 int32 DefenderCasualties);

  UFUNCTION()
  virtual void HandleActiveFighterChanged(AFighterPawn *NewFighter);

  UFUNCTION()
  virtual void HandleRoundStarted(int32 RoundNumber,
                                  ESkaldFaction InitiativeWinner);

  UFUNCTION()
  void HandleInitiativePhaseStarted(int32 RoundNumber);

  UFUNCTION()
  void HandleInitiativeRollCompleted(int32 RoundNumber, int32 AttackerRoll,
                                     int32 DefenderRoll,
                                     ESkaldFaction InitiativeWinner);

  UFUNCTION()
  void HandleInitiativeRollRequested();

  UFUNCTION()
  void HandleStrategicInitiativeRollRequested();

  UFUNCTION(Client, Reliable)
  void ClientPromptStrategicInitiative(int32 RoundNumber, int32 RollValue,
                                       bool bWonInitiative);

  UFUNCTION(Server, Reliable)
  void ServerConfirmStrategicInitiativeRollReady();

  UFUNCTION(Client, Reliable)
  void ClientDisplayStrategicInitiativeResult(int32 RoundNumber, int32 RollValue,
                                              bool bWonInitiative);

  UFUNCTION(Client, Reliable)
  void ClientClearStrategicInitiativeOverlay();

  UFUNCTION(Client, Reliable)
  void ClientShowPrepareForBattle(const FPrepareForBattlePromptData &PromptData);

  UFUNCTION(Client, Reliable)
  void ClientHidePrepareForBattle();

  /** Local entry points so standalone/authority controllers can trigger the
   *  prepare-for-battle flow without relying on client RPC delivery. */
  void ShowPrepareForBattlePromptLocal(
      const FPrepareForBattlePromptData &PromptData);
  void HidePrepareForBattlePromptLocal();

  /** Server-side processing of an attack request. */
  UFUNCTION(Server, Reliable)
  void ServerHandleAttack(int32 FromID, int32 ToID, int32 ArmySent,
                          bool bUseSiege);

  UFUNCTION(Server, Reliable)
  void ServerSetReadyForBattle(bool bReady);

  /** Handle HUD siege build requests. */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleBuildSiegeRequested(int32 TerritoryID, ESiegeWeapon SiegeType);

  /** Server-side processing of a siege build request. */
  UFUNCTION(Server, Reliable)
  void ServerBuildSiege(int32 TerritoryID, ESiegeWeapon SiegeType);

  /** Server-side processing of a treasure dig request. */
  UFUNCTION(Server, Reliable)
  void ServerDigTreasure(int32 TerritoryID);

  /** Server-side processing of a move request. */
  UFUNCTION(Server, Reliable)
  void ServerHandleMove(int32 FromID, int32 ToID, int32 Troops);

  /** Notify the client of the result of a move request. */
  UFUNCTION(Client, Reliable)
  void ClientHandleMoveOutcome(bool bSuccess, int32 FromID, int32 ToID,
                               const FString &Message);

  /** Server-side processing of a unit deployment. */
  UFUNCTION(Server, Reliable)
  void ServerDeployUnits(int32 TerritoryID, int32 Amount);

  /** Server-side processing of a territory selection. Pass -1 to deselect. */
  UFUNCTION(Server, Reliable)
  void ServerSelectTerritory(int32 TerritoryID);

  /** Client-side update for a territory selection. Pass -1 to clear. */
  UFUNCTION(Client, Reliable)
  void ClientSelectTerritory(int32 TerritoryID);

  /** Phase change handlers. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void HandleAttackPhase();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void HandleEngineeringPhase();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void HandleTreasurePhase();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void HandleMovementPhase();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void HandleEndTurnPhase();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void HandleRevoltPhase();

  /** Reference to the game's turn manager.
   *  Exposed to Blueprints so BP_Skald_PlayerController can bind to
   *  turn events without keeping an external pointer that might be
   *  uninitialised.
   */
protected:
  UPROPERTY(EditInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_TurnManager,
            Category = "Turn", meta = (ExposeOnSpawn = true))
  TObjectPtr<ATurnManager> TurnManager;

  /** Helper to update cached state whenever the replicated turn manager changes. */
  void ApplyTurnManager(ATurnManager *Manager);

  void RegisterPendingReadyPromptRetry();
  void HandlePendingReadyPromptRetry();
  bool TryShowPendingReadyPrompt();
  bool ShouldDisplayPrepareForBattlePrompt(
      const FPrepareForBattlePromptData &PromptData);
  void ResetPendingReadyPromptState();
  void ShowPrepareForBattlePromptLocal_Internal(
      const FPrepareForBattlePromptData &PromptData);

  /** Set up the main HUD widget for the local player. */
  virtual void InitializeHUDWidget();

private:
  /** Display the stored strategic initiative roll if one is pending. */
  void ShowPendingStrategicInitiativeResult();

  ASkald_BattleGameMode *ResolveBattleGameMode();

  /** Cache references to key game singletons and bind delegates. */
  void CacheGameReferences();

  /** Create the faction selection widget for the local player. */
  void InitializeChoosePlayerWidget();

  /** Determine whether lobby data already contains the local player's choices. */
  bool ShouldAutoLockInFromLobby() const;

  /** Initialize the player state using lobby selections instead of the legacy widget. */
  void AutoInitializeFromLobbySelection();

  void InitializeBattleHUD();
  void ShowOverworldHUD();
  void HideOverworldHUDForBattle();
  UGridOverlayComponent *FindGridOverlay() const;

  /** Attempt to locate the world map and bind to its selection event. */
  void TryBindWorldMap();

  /** Cached pointer to the active world map to manage delegate bindings safely. */
  TWeakObjectPtr<AWorldMap> CachedWorldMap;

  /** Timer used to poll for the world map actor until it exists. */
  FTimerHandle WorldMapSearchHandle;

  /** Whether the battle HUD is currently visible. */
  bool bBattleHUDVisible;

  /** Whether we should display the battle HUD as soon as fighters activate. */
  bool bBattleHUDReadyToShow;

  /** Whether the current map is a battle map. */
  UPROPERTY(BlueprintReadOnly, Category = "Turn",
            meta = (AllowPrivateAccess = "true"))
  bool bIsBattleMap = false;

  /** Detect if the current level is a battle map and update bIsBattleMap. */
  void DetectBattleMap();

  void BuildPlayerDataArray(TArray<FS_PlayerData> &OutPlayers) const;

protected:
  /**
   * Validation helper surfaced for derived types when automation tests are
   * enabled.  The automation test controller derives from
   * ASkaldPlayerController so exposing the helper via protected visibility
   * keeps the production API unchanged while letting tests reuse the logic.
   */
  bool ValidateAttack(int32 FromID, int32 ToID, int32 ArmySent, bool bUseSiege,
                      FString *OutError);

private:
  bool ValidateMoveRequest(AWorldMap *WorldMap, int32 FromID, int32 ToID,
                           int32 Troops, ATerritory *&OutSource,
                           ATerritory *&OutTarget, FString &OutError) const;

  UFUNCTION(Client, Reliable)
  void NotifyActionError(const FString &Message);

  /** Make sure the battle HUD is created and visible. */
  void EnsureBattleHUDVisible();

  /** Spawn or refresh the fighter selection UI. */
  void ShowFighterSelectionUI(int32 MaxBudget, ESkaldFaction Faction);

  /** Ensure a valid TurnManager is available, attempting reacquisition if
   * needed. */
  bool EnsureTurnManager(const TCHAR *Caller);

  /** Create the fighter selection widget if we are on a battle map and it is
   * not already shown. */
  void InitializeFighterSelectionIfNeeded();

  void CancelCommandMode();
  void HighlightClickedCell(UGridOverlayComponent *Grid, const FIntPoint &Cell);
  void SetSelectedFighter(AFighterPawn *Fighter, bool bForce = false);
  void ClearSelectedFighter();
  void UpdateBattleHUDSelection();
  void UpdateBattleHUDButtons();
  void UpdateBattleRoundDisplay(int32 RoundNumber, ESkaldFaction InitiativeWinner);
  void UpdateBattleTerritoryLabel();
  void UpdateBattlePlayersTurnDisplay();
  void RefreshLockedInFighterList();
  void RefreshLockedInFighterTurnStates();
  void RegisterObservedFighter(AFighterPawn *Fighter);
  void HandleActorSpawned(AActor *SpawnedActor);
  void UpdateLockedInSelectionHighlight();
  void UpdateLockedInActiveHighlight();
  UFUNCTION()
  void HandleAttackResolved(AFighterPawn *Attacker, AFighterPawn *Defender,
                            const FDiceRollResult &Result);

  void PlayAttackFeedback(AFighterPawn *Attacker, AFighterPawn *Defender,
                          const FDiceRollResult &Result);
  void PlayDiceOutcomeFeedback(AFighterPawn *Attacker, AFighterPawn *Defender,
                               const FDiceRollOutcome &Outcome);
  void TriggerFighterDeathFeedback(AFighterPawn *Fighter);
  UNiagaraSystem *ResolveFighterDeathEffect(ESkaldFaction Faction) const;
  UNiagaraSystem *ResolveFighterDeathSplatterEffect(ESkaldFaction Faction) const;
  UNiagaraSystem *ResolveHitDecalEffect(ESkaldFaction Faction) const;
  UNiagaraSystem *ResolveNaturalSixDecalEffect(ESkaldFaction Faction) const;
  void SpawnTimedNiagaraSystem(UNiagaraSystem *Effect, const FVector &Location,
                               float LifetimeSeconds, const FVector &Scale);
  void TriggerHighStakesCritFeedback(AFighterPawn *Attacker,
                                     AFighterPawn *Defender,
                                     const FDiceRollResult &Result);

  UFUNCTION()
  void HandleDiceResolutionComplete(AFighterPawn *Attacker,
                                     AFighterPawn *Defender,
                                     const FDiceRollResult &Result);
  UFUNCTION()
  void HandleDiceOutcomeRevealed(AFighterPawn *Attacker,
                                 AFighterPawn *Defender,
                                 const FDiceRollOutcome &Outcome,
                                 int32 RevealIndex);
  UFUNCTION()
  void HandleAttackRejected(AFighterPawn *Attacker, AFighterPawn *Defender,
                            const FText &Reason);
  UFUNCTION()
  void HandleLockedInEntrySelected(AFighterPawn *Fighter);
  UFUNCTION()
  void HandleTrackedFighterDestroyed(AActor *DestroyedActor);
  bool IsFriendlyFighter(const AFighterPawn *Fighter) const;
  void DetermineControlledBattleSide();
  void TryDispatchPendingAttackPresentationNotifications();
  void HandlePendingPresentationTimerTick();

  UPROPERTY()
  TObjectPtr<AFighterPawn> SelectedFighter;

  UPROPERTY()
  TObjectPtr<AFighterPawn> LockedActiveFighter;

  /** Friendly fighters tracked for HUD list synchronization. */
  TSet<TWeakObjectPtr<AFighterPawn>> ObservedFriendlyFighters;

  /** Delegate handle used to watch for fighter spawns. */
  FDelegateHandle FighterSpawnedHandle;

  bool bControlsAttackerSide = false;
  bool bControlsDefenderSide = false;

  /** Cached copy of the last initiative value rolled locally so it can be
   *  re-presented once the server confirms the result. */
  int32 LastLocalInitiativeRoll = 0;

  /** Cached initiative value pending presentation on the strategic HUD. */
  int32 PendingStrategicInitiativeRoll = 0;

  /** Round index associated with the pending strategic initiative roll. */
  int32 PendingStrategicInitiativeRound = 0;

  /** Whether the cached strategic initiative roll was the winning value. */
  bool bPendingStrategicInitiativeWin = false;

  /** Whether the main HUD is waiting for the player to roll strategic initiative. */
  bool bAwaitingStrategicInitiativeRoll = false;

  /** Cached prompt data that should be displayed once the HUD is available. */
  FPrepareForBattlePromptData PendingReadyPrompt;

  /** Whether a prepare-for-battle prompt is awaiting HUD initialization. */
  bool bPendingReadyPrompt = false;

  /** Last strategic round index that triggered the initiative prompt audio. */
  int32 LastStrategicInitiativeSoundRound = INDEX_NONE;

  /** Last grid battle round index that triggered the initiative prompt audio. */
  int32 LastBattleInitiativeSoundRound = INDEX_NONE;

  /** State used to avoid replaying the grid turn cue when the prompt has not changed. */
  int32 LastBattleTurnSoundRound = INDEX_NONE;
  bool bLastBattleTurnSoundWasAttacker = false;
  int32 LastBattleTurnSoundAvailableCount = INDEX_NONE;

  /** Timer that polls for combat presentation completion (dice + floaters). */
  FTimerHandle BattlePresentationMonitorHandle;

  /** Timer used to retry showing a pending prepare-for-battle prompt. */
  FTimerHandle PendingReadyPromptRetryHandle;

  /** Number of pending presentation completions awaiting acknowledgment. */
  int32 PendingAttackPresentationNotifications = 0;

  /** Cached battle manager awaiting presentation completion notification. */
  TWeakObjectPtr<UGridBattleManager> PendingPresentationBattleManager;
};
