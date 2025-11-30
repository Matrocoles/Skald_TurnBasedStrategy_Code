#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "GridBattleManager.h"
#include "SkaldTypes.h"
#include "Abilities/SkaldAbilityTypes.h"
#include "TimerManager.h"
#include "Delegates/Delegate.h"
#include "Camera/CameraShakeBase.h"
#include "UObject/WeakObjectPtr.h"
#include "Templates/SharedPointer.h"
#include "SkaldDiceOverlayWidget.h"
#include "SkaldDiceResultWidget.h"
#include "FactionCursorData.h"
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
class USkaldAbilityComponent;
class AActor;
class UGridOverlayComponent;
class UWorld;
class ASkald_BattleGameMode;
class USoundBase;
class UCameraShakeBase;
class UNiagaraComponent;
class UNiagaraSystem;
class UTexture2D;
class UFactionCursorData;
struct FFactionCursorDefinition;
class ASkald_PlayerCharacter;
class USkaldDiceOverlayWidget;
class USkaldDiceResultWidget;
class USkaldDiceManager;
class ICursor;
class UImage;
class UCanvasPanel;
class UFactionCursorWidget;
struct FSkaldTravelState;

/** Command issued by the player during a battle. */
UENUM()
enum class EBattleCommandMode : uint8 {
  None,
  Move,
  Disengage,
  Attack,
  VeilStep,
  AbilityTargetEnemy,
  AbilityTargetAlly,
  AbilityTargetCell
};

struct FSkaldAbilityTargetingInfo {
  EBattleCommandMode CommandMode = EBattleCommandMode::None;
  int32 RangeOverride = INDEX_NONE;
  bool bRequireLineOfSight = true;
  bool bAllowSelfTarget = false;
  bool bAllowEmptyCell = false;
  bool bPerformAttack = true;
};

struct FPendingAbilityCommand {
  TWeakObjectPtr<AFighterPawn> SourceFighter;
  ESkaldAbilitySlot Slot = ESkaldAbilitySlot::Ability1;
  FName AbilityId = NAME_None;
  FSkaldAbilityTargetingInfo Targeting;
};

struct FBattleResultDisplayData {
  bool bValid = false;
  bool bPlayerWon = false;
  bool bPlayerLost = false;
  bool bPlayerWasAttacker = true;
  int32 AttackerCasualties = 0;
  int32 DefenderCasualties = 0;
  FLinearColor PlayerFactionColor = FLinearColor::White;
  FText PlayerNameText;
  FText PlayerFactionText;
  FText EnemyNameText;
  FText EnemyFactionText;
};

/** Simple Slate-backed widget used to render custom cursor art. */
UCLASS()
class SKALD_API UFactionCursorWidget : public UUserWidget {
  GENERATED_BODY()

public:
  void InitializeCursor(UTexture2D *InTexture, const FVector2D &InHotspot,
                        const FVector2D &InDrawSize);

protected:
  virtual TSharedRef<SWidget> RebuildWidget() override;
  virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
  void RefreshCursorAppearance();

  UPROPERTY(Transient)
  TObjectPtr<UImage> CursorImage = nullptr;

  UPROPERTY(Transient)
  TObjectPtr<UCanvasPanel> RootPanel = nullptr;

  UPROPERTY(Transient)
  TObjectPtr<UTexture2D> CursorTexture = nullptr;

  FVector2D CursorHotspot = FVector2D::ZeroVector;
  FVector2D CursorDrawSize = FVector2D::ZeroVector;
};

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
  /** Allow the game state to initialize fighter selection when battle data replicates. */
  friend class ASkaldGameState;
  /** Allow the turn manager to trigger HUD refresh when phase state replicates. */
  friend class ATurnManager;
#if defined(WITH_AUTOMATION_TESTS) && WITH_AUTOMATION_TESTS
  /** Permit the automation test subclass to access validation helpers. */
  friend class ATestPlayerController;
#endif

public:
  ASkaldPlayerController();

  virtual void OnRep_PlayerState() override;

  virtual void OnPossess(APawn *InPawn) override;

  virtual void PlayerTick(float DeltaTime) override;

  virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  UFUNCTION(BlueprintCallable, Category = "Turn")
  virtual void StartTurn();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  virtual void EndTurn();

  /**
   * Server authoritative handler for ending the active player's turn.
   * Validates ownership using replicated turn state to avoid host-only
   * advancement paths.
   */
  UFUNCTION(Server, Reliable)
  void ServerEndTurn();

  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn")
  bool IsMyTurn() const;

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void EndPhase();

  /** Request the server to end the current phase for this controller. */
  UFUNCTION(Server, Reliable)
  void ServerEndPhase();

  /** Inform the server that this client has closed the battle results widget. */
  UFUNCTION(Server, Reliable)
  void ServerAcknowledgeBattleResults();

  // === Manual Attack Roll UI Flow ===

  UFUNCTION(Client, Reliable)
  void ClientShowAttackRollButton(class AFighterPawn* Attacker,
                                  bool bAutoTriggerRoll);

  UFUNCTION(Client, Reliable)
  void ClientHideAttackRollButton();

  UFUNCTION(Server, Reliable)
  void ServerTriggerManualAttackRoll(class AFighterPawn* Attacker);

  UFUNCTION(Server, Reliable)
  void ServerNotifyAIAttackOverviewComplete(class AFighterPawn* Attacker);

  bool IsFriendlyFighter(const AFighterPawn *Fighter) const;

  /** Set the turn manager responsible for sequencing play. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void SetTurnManager(ATurnManager *Manager);

  /** RepNotify hook for TurnManager replication. */
  UFUNCTION()
  void OnRep_TurnManager();

  DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectedFighterChanged,
                                              AFighterPawn *, Fighter);

  /** Fired whenever the controller selects a new fighter in battle. */
  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Events")
  FOnSelectedFighterChanged OnSelectedFighterChanged;

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void ShowTurnAnnouncement(const FString &PlayerName, bool bIsMyTurn);

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void NotifyTurnEnded(const FString &PlayerName);

  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleTerritorySelected(ATerritory *Terr);

  /** Resolve the local player's stable identifier once their PlayerState finishes registering. */
  int32 GetResolvedLocalPlayerId() const;

  /** Returns true when the local controller has a registered PlayerState. */
  bool HasResolvedLocalPlayerId() const;

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

  /** Applies the cursor visual/audio/FX for the currently selected faction. */
  UFUNCTION(BlueprintCallable, Category = "Cursor")
  void ApplyFactionCursor();

  /** Clears any faction cursor overrides returning to the default pointer. */
  UFUNCTION(BlueprintCallable, Category = "Cursor")
  void ClearFactionCursor();

  /** Plays the configured hover sound for the active faction cursor. */
  UFUNCTION(BlueprintCallable, Category = "Cursor")
  void PlayCursorHoverSound();

  /** Plays the configured click sound for the active faction cursor. */
  UFUNCTION(BlueprintCallable, Category = "Cursor")
  void PlayCursorClickSound();

  /** Attempt to trigger the active fighter ability mapped to the given slot. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  bool TryUseAbilitySlot(ESkaldAbilitySlot Slot);

  UFUNCTION(Server, Reliable)
  void ServerTryUseAbilitySlot(ESkaldAbilitySlot Slot);

  /** Handle ability input routed from characters or widgets. */
  UFUNCTION()
  void HandleAbilityInput(ESkaldAbilitySlot Slot);

  /** Retrieve the turn manager controlling this player. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn")
  ATurnManager *GetTurnManager() const { return TurnManager; }

  /** Handle a locked-in fighter entry being selected from the HUD. */
  void RequestLockedInEntrySelection(AFighterPawn *Fighter);

  /** Handle an enemy locked-in fighter entry being selected from the HUD. */
  void RequestEnemyLockedInEntrySelection(AFighterPawn *Fighter);

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

  /** Overlay widget used to present dice rolls without additional blueprint wiring. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI|Dice")
  TSubclassOf<USkaldDiceOverlayWidget> DiceOverlayWidgetClass;

  /** Compact widget used to display initiative roll summaries. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI|Dice")
  TSubclassOf<USkaldDiceResultWidget> DiceResultWidgetClass;

  /** Automatically trigger dice overlay presentations for combat events. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI|Dice")
  bool bAutoPresentDiceRolls = true;

  /** Automatically trigger initiative presentations using the dice subsystem. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI|Dice")
  bool bAutoPresentInitiativeRolls = true;

  /** Duration to keep the initiative summary widget visible after an update. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI|Dice", meta = (ClampMin = "0.0"))
  float InitiativeResultLingerSeconds = 2.f;

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

  /** Sound to play for the local player when an enemy world map turn begins. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Audio")
  USoundBase *EnemyTurnSFX = nullptr;

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

  /** Cached pointer to the active Battle HUD widget (runtime reference). */
  UPROPERTY(BlueprintReadOnly, Category = "UI")
  TObjectPtr<UBattleHUDWidget> BattleHUD;

  /** Dice overlay widget automatically spawned from class defaults. */
  UPROPERTY(Transient)
  TObjectPtr<USkaldDiceOverlayWidget> DiceOverlayWidget;

  /** Initiative summary widget automatically spawned from class defaults. */
  UPROPERTY(Transient)
  TObjectPtr<USkaldDiceResultWidget> DiceResultWidget;

  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UInGameMenuWidget> InGameMenuWidget;

  /** Battle result widget displayed after combat resolves. */
  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  UUserWidget *BattleResultWidget;

  /** Cached data used to rebuild the battle result widget after returning home. */
  FBattleResultDisplayData CachedBattleResultDisplayData;

  /** True when the overworld should recreate the battle result widget. */
  bool bPendingOverworldBattleResults = false;

  /** True when closing the battle result widget should notify the server. */
  bool bAwaitingBattleResultCloseAck = false;

  /** Data-driven cursor configuration used for faction specific visuals. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cursor",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UFactionCursorData> FactionCursorData = nullptr;

  /** Faction currently applied to the local player's cursor. */
  UPROPERTY(BlueprintReadOnly, Category = "Cursor",
            meta = (AllowPrivateAccess = "true"))
  ESkaldFaction CurrentFaction = ESkaldFaction::None;

  /** Widget instance responsible for rendering the custom cursor texture. */
  UPROPERTY(Transient)
  TObjectPtr<UFactionCursorWidget> ActiveCursorWidget = nullptr;

  /** Niagara component providing the active cursor trail effect. */
  UPROPERTY()
  TObjectPtr<UNiagaraComponent> ActiveCursorTrailFX = nullptr;

  /** Template Niagara system backing the active cursor trail. */
  UPROPERTY()
  TWeakObjectPtr<UNiagaraSystem> ActiveCursorTrailTemplate;

  /** Offset applied to the active cursor trail FX in world space. */
  FVector ActiveCursorTrailOffset = FVector::ZeroVector;

  /** Tracks whether the cursor was hovering an interactable widget last tick. */
  bool bWasHoveringInteractable = false;

  void ShowBattleResultWidget(const FBattleResultDisplayData &DisplayData);
  void ClearBattleResultWidget();

  UFUNCTION()
  void HandleBattleResultWidgetClosed();

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

  /** Retry handle used when core replicated references are not yet available. */
  FTimerHandle GameReferenceRetryHandle;

  /** Cached reference to the active grid battle manager. */
  mutable TWeakObjectPtr<UGridBattleManager> CachedBattleManager;

  /** Current command selection when issuing grid battle orders. */
  EBattleCommandMode CurrentCommandMode;

  /** Ensures ServerInitPlayerState and lock-in handling run only once. */
  bool bHasInitialized;

  /** Default mouse capture behavior for this controller. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
  EMouseCaptureMode DefaultMouseCaptureMode;

  virtual void BeginPlay() override;

  virtual void SetupInputComponent() override;

  UFUNCTION()
  void HandleWorldBeginPlay(UWorld *LoadedWorld);

  void ExecuteLocalTurnStart();
  void ShowTurnAnnouncementLocal(const FString &PlayerName, bool bIsMyTurn);
  void NotifyTurnEndedLocal(const FString &PlayerName);
  void HandleAttackPhaseLocal();
  void HandleEngineeringPhaseLocal();
  void HandleTreasurePhaseLocal();
  void HandleMovementPhaseLocal();
  void HandleEndTurnPhaseLocal();
  void HandleRevoltPhaseLocal();

  /** Shared implementation for ending the current phase on the server. */
  void HandleEndPhaseInternal();

  FDelegateHandle PostWorldBeginPlayHandle;

  /** Begin selecting a move destination. */
  UFUNCTION()
  void BeginMoveMode();

  /** Begin selecting a disengage destination. */
  UFUNCTION()
  void BeginDisengageMode();

  /** Begin selecting an attack target. */
  UFUNCTION()
  void BeginAttackMode();

  /** Handle the player clicking on the grid. */
  UFUNCTION()
  void HandleGridClick();

  /** Find a fighter occupying the specified grid cell. */
  AFighterPawn *FindFighterAtCell(const FIntPoint &Cell) const;

  /** Retrieve the active grid battle manager, caching the result. */
  UGridBattleManager *GetBattleManager() const;

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

  /** Handle HUD retreat requests. */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleRetreatRequested();

  /** Handle HUD retreat destination confirmations. */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleRetreatDestinationSelected(int32 TerritoryID);

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

  /** React to replicated battle participant updates. */
  UFUNCTION()
  void HandleBattleEntriesUpdated();

  /** React to faction selections in the game instance. */
  UFUNCTION()
  void HandleFactionsUpdated();

  /** React to the game entering or exiting the streamed battle map. */
  UFUNCTION()
  virtual void HandleBattleMapStateChanged(bool bInBattleMap);

  /** React to world state changes broadcast by the turn manager. */
  UFUNCTION()
  void HandleWorldStateChanged();

  /** Getter for the active Battle HUD widget. */
  UFUNCTION(BlueprintCallable, Category = "UI")
  UBattleHUDWidget* GetBattleHUD() const { return BattleHUD; }

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
  void HandleReplicatedBattleRound(int32 RoundNumber, ESkaldFaction InitiativeWinner, int32 AttackerActivations,
                                   int32 DefenderActivations, bool bIsAttackerTurn);

  UFUNCTION()
  void HandleInitiativePhaseStarted(int32 RoundNumber);

  UFUNCTION()
  void HandleInitiativeRollCompleted(int32 RoundNumber, int32 AttackerRoll,
                                     int32 DefenderRoll,
                                     ESkaldFaction InitiativeWinner);

  UFUNCTION()
  void HandleInitiativeRollRequested();

  UFUNCTION()
  void HandleAttackRollRequested();

  UFUNCTION()
  void HandleStrategicInitiativeRollRequested();

  UFUNCTION(Client, Reliable)
  void ClientPromptStrategicInitiative(int32 RoundNumber, int32 RollValue,
                                       bool bWonInitiative);

  UFUNCTION(Server, Reliable)
  void ServerConfirmStrategicInitiativeRollReady();

  UFUNCTION(Client, Reliable)
  void ClientDisplayStrategicInitiativeResult(int32 RoundNumber, int32 RollValue,
                                              int32 EnemyRoll, bool bWonInitiative);

  UFUNCTION(Client, Reliable)
  void ClientClearStrategicInitiativeOverlay();

  UFUNCTION(Client, Reliable)
  void ClientShowPrepareForBattle(const FPrepareForBattlePromptData &PromptData);

  UFUNCTION(Client, Reliable)
  void ClientHidePrepareForBattle();

  UFUNCTION(Client, Reliable)
  void ClientBeginRetreatSelection(int32 DefendingTerritoryID,
                                   const TArray<int32> &CandidateTerritoryIDs);

  UFUNCTION(Client, Reliable)
  void ClientCompleteRetreat();

  UFUNCTION(Client, Reliable)
  void ClientRetreatFailed(const FText &Message);

  UFUNCTION(Client, Reliable)
  void ClientEnemyRetreated();

  UFUNCTION(Client, Reliable)
  void ClientShowTurnAnnouncement(const FString &PlayerName, bool bIsMyTurn);

  UFUNCTION(Client, Reliable)
  void ClientNotifyTurnEnded(const FString &PlayerName);

  UFUNCTION(Client, Reliable)
  void ClientHandlePhaseChanged(ETurnPhase NewPhase);

  /** Local entry points so standalone/authority controllers can trigger the
   *  prepare-for-battle flow without relying on client RPC delivery. */
  virtual void ShowPrepareForBattlePromptLocal(
      const FPrepareForBattlePromptData &PromptData);
  virtual void HidePrepareForBattlePromptLocal();

  /** Server-side processing of an attack request. */
  UFUNCTION(Server, Reliable)
  void ServerHandleAttack(int32 FromID, int32 ToID, int32 ArmySent,
                          bool bUseSiege);

  UFUNCTION(Server, Reliable)
  void ServerSetReadyForBattle(bool bReady);

  UFUNCTION(Server, Reliable)
  void ServerRequestRetreat();

  UFUNCTION(Server, Reliable)
  void ServerConfirmRetreatDestination(int32 TerritoryID);

  // ============================================================
  // Manual Dice Roll: Player sends manual attack roll result to server
  // ============================================================
  UFUNCTION(Server, Reliable)
  void ServerSubmitManualAttackRoll(AFighterPawn* Attacker, int32 RollValue);

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

  /** Request the local player's selection be sent to the server for validation. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void RequestSelectTerritory(class ATerritory *Territory);

  /** Server-side processing of a territory selection. Pass nullptr to deselect. */
  UFUNCTION(Server, Reliable)
  void ServerSelectTerritory(class ATerritory *Territory);

  /** Request the pending battle payload from the host if it failed to replicate. */
  UFUNCTION(Server, Reliable)
  void ServerRequestPendingBattleState();

  /** Apply the pending battle payload sent from the host. */
  UFUNCTION(Client, Reliable)
  void ClientApplyPendingBattleState(const FS_BattlePayload &Battle,
                                     const FSkaldTravelState &TravelState,
                                     bool bBattleMapActive);

  UFUNCTION(Client, Reliable)
  void ClientStartPhysicalDiceRoll(const FGuid &RollId,
                                   const TArray<int32> &PlayerResults,
                                   const TArray<int32> &EnemyResults,
                                   bool bForInitiative, FLinearColor PlayerColor,
                                   FLinearColor EnemyColor);

  static void BroadcastPhysicalDiceRoll(UWorld *World, const FGuid &RollId,
                                        int32 PlayerDice, int32 EnemyDice,
                                        bool bForInitiative,
                                        FLinearColor PlayerColor,
                                        FLinearColor EnemyColor);

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

protected:
  /** Reference to the game's turn manager.
   *  Exposed to Blueprints so BP_Skald_PlayerController can bind to
   *  turn events without keeping an external pointer that might be
   *  uninitialised.
   */
  UPROPERTY(EditInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_TurnManager,
            Category = "Turn", meta = (ExposeOnSpawn = true))
  TObjectPtr<ATurnManager> TurnManager;

  /** Keep HUD turn/phase widgets aligned with replicated game state. */
  void RefreshTurnDataFromState();
  UFUNCTION()
  void HandleTurnIndexChanged(int32 NewIndex);
  UFUNCTION()
  void HandleActivePlayerChanged(int32 NewActivePlayerId);
  void HandleReplicatedTurnOwnership();
  void HandleReplicatedTurnStart();
  void HandleReplicatedPhaseChange(ETurnPhase NewPhase);
  void HandleReplicatedBattlePayload();

  /** Helper to update cached state whenever the replicated turn manager changes. */
  void ApplyTurnManager(ATurnManager *Manager);
  ATurnManager *FindTurnManagerActor() const;

  void RegisterPendingReadyPromptRetry();
  void HandlePendingReadyPromptRetry();
  bool TryShowPendingReadyPrompt();
  bool ShouldDisplayPrepareForBattlePrompt(
      const FPrepareForBattlePromptData &PromptData);
  void ResetPendingReadyPromptState();
  void ShowPrepareForBattlePromptLocal_Internal(
      const FPrepareForBattlePromptData &PromptData);

  virtual void OnBeginRetreatSelection(int32 DefendingTerritoryID,
                                       const TArray<int32> &CandidateTerritoryIDs);

  /** Set up the main HUD widget for the local player. */
  virtual void InitializeHUDWidget();

public:
  void BeginRetreatSelectionLocal(int32 DefendingTerritoryID,
                                  const TArray<int32> &CandidateTerritoryIDs);
  void CompleteRetreatSelectionLocal();
  virtual void NotifyRetreatFailed(const FText &Message);
  void NotifyEnemyRetreated();
private:
  /** Display the stored strategic initiative roll if one is pending. */
  void ShowPendingStrategicInitiativeResult();

  ASkald_BattleGameMode *ResolveBattleGameMode();

  /** Cache references to key game singletons and bind delegates. */
  void CacheGameReferences();

  /** Create the faction selection widget for the local player. */
  void InitializeChoosePlayerWidget();

  /** Initialize the player state using lobby selections instead of the legacy widget. */
  void AutoInitializeFromLobbySelection();

  void InitializeBattleHUD();
  void ShowOverworldHUD();
  void HideOverworldHUDForBattle();
  UGridOverlayComponent *FindGridOverlay() const;

  /** Attempt to locate the world map and bind to its selection event. */
  void TryBindWorldMap();

  /** Reapply the most recent local territory selection once IDs/world map resolve. */
  bool RefreshLocalTerritorySelection();

  /** Resolve a player's stable ID, falling back to hashed net IDs before replication. */
  int32 ResolveStablePlayerId(const class ASkaldPlayerState *InPlayerState) const;

  /** Handle playing the click sound when interacting with UI. */
  void HandleCursorClickSound();

  /** Clear map or battle selections when the clear-selection keybind is used. */
  void HandleClearSelectionPressed();

  /** Update cursor trail FX to follow the hardware cursor. */
  void UpdateCursorFX();

  /** Determine which faction cursor should be active and apply it. */
  void RefreshFactionCursorFromState();

  /** Resolve the cursor definition for the currently active faction. */
  const FFactionCursorDefinition *ResolveCursorDefinition() const;

  /** Cached pointer to the active world map to manage delegate bindings safely. */
  TWeakObjectPtr<AWorldMap> CachedWorldMap;

  /** Replays territory selections made while the world map was still registering territories. */
  void RetryPendingTerritorySelection();

  /** Queued territory selection to replay once the world map finishes spawning. */
  int32 PendingTerritorySelectionId = INDEX_NONE;

  /** Timer used to poll for world map readiness after an early selection arrives. */
  FTimerHandle PendingTerritorySelectionHandle;

  /** Whether we need to retry restoring the local player's territory selection. */
  bool bPendingLocalSelectionRefresh = false;

  /** Timer used to poll for the world map actor until it exists. */
  FTimerHandle WorldMapSearchHandle;

  /** Timer used to retry lobby-driven initialization until selections replicate. */
  FTimerHandle LobbyAutoInitHandle;

  /** Whether the battle HUD is currently visible. */
  bool bBattleHUDVisible;

  /** Whether we should display the battle HUD as soon as fighters activate. */
  bool bBattleHUDReadyToShow;

  /** Whether the current map is a battle map. */
  UPROPERTY(BlueprintReadOnly, Category = "Turn",
            meta = (AllowPrivateAccess = "true"))
  bool bIsBattleMap = false;

  /** Tracks if ExecuteLocalTurnStart has been triggered for the active turn. */
  bool bLocalTurnActive = false;

  /** Prevents redundant pending battle state requests while waiting for a reply. */
  bool bPendingBattleStateRequest = false;

  /** Detect if the current level is a battle map and update bIsBattleMap. */
  void DetectBattleMap();

  void RequestBattleStateIfNeeded();

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
  struct FPendingDiceFeedbackState {
    TWeakObjectPtr<AFighterPawn> Attacker;
    TWeakObjectPtr<AFighterPawn> Defender;
    FDiceRollResult Result;
    int32 NextRevealIndex = 0;
    int32 SimulatedDefenderHealth = 0;
    bool bTriggeredDeathFeedback = false;
    bool bTriggeredHighStakesFeedback = false;
  };

  FPendingDiceFeedbackState *FindPendingDiceFeedbackState(
      AFighterPawn *Attacker, AFighterPawn *Defender);
  void RemovePendingDiceFeedbackState(AFighterPawn *Attacker,
                                      AFighterPawn *Defender);

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
  void HandleEnemyLockedInEntrySelected(AFighterPawn *Fighter);
  UFUNCTION()
  void HandleTrackedFighterDestroyed(AActor *DestroyedActor);
  bool TryBeginVeilStepTargeting(ESkaldAbilitySlot Slot);
  bool IsVeilStepAbility(const USkaldAbilityComponent *AbilityComp,
                         ESkaldAbilitySlot Slot) const;
  void BeginVeilStepTargeting(AFighterPawn *Fighter, ESkaldAbilitySlot Slot);
  void HighlightVeilStepOptions(AFighterPawn *Fighter,
                                UGridOverlayComponent *Grid) const;
  bool FindBestVeilStepAnchor(AFighterPawn *Fighter, UGridOverlayComponent *Grid,
                              const FIntPoint &ClickedCell,
                              FIntPoint &OutAnchor) const;
  bool IsVeilStepDestinationValid(AFighterPawn *Fighter,
                                  UGridOverlayComponent *Grid,
                                  const FIntPoint &Anchor) const;
  bool ExecuteVeilStepInternal(AFighterPawn *Fighter, ESkaldAbilitySlot Slot,
                               const FIntPoint &TargetAnchor, FText *OutError);
  UFUNCTION(Server, Reliable)
  void ServerExecuteVeilStep(AFighterPawn *Fighter, ESkaldAbilitySlot Slot,
                             FIntPoint TargetAnchor);
  void CancelAbilityCommand();
  bool TryBeginAbilityCommand(AFighterPawn *Fighter, ESkaldAbilitySlot Slot,
                              const FSkaldAbilityTargetingInfo &Targeting,
                              const FName AbilityId);
  void HighlightAbilityCommandOptions(const FPendingAbilityCommand &Command,
                                      UGridOverlayComponent *Grid) const;
  bool ValidateAbilityTargetCell(const FPendingAbilityCommand &Command,
                                 const FIntPoint &Cell,
                                 FText &OutError) const;
  bool ValidateAbilityTargetFighter(const FPendingAbilityCommand &Command,
                                    AFighterPawn *Target,
                                    FText &OutError) const;
  FSkaldAbilityTargetingInfo GetAbilityTargetingInfo(FName AbilityId) const;
  bool ExecuteAbilityCommandInternal(const FPendingAbilityCommand &Command,
                                     AFighterPawn *TargetFighter,
                                     const FIntPoint *TargetCell,
                                     FText *OutError);
  UFUNCTION(Server, Reliable)
  void ServerExecuteAbilityOnFighter(AFighterPawn *Source,
                                     ESkaldAbilitySlot Slot,
                                     AFighterPawn *Target);
  UFUNCTION(Server, Reliable)
  void ServerExecuteAbilityAtCell(AFighterPawn *Source,
                                  ESkaldAbilitySlot Slot, FIntPoint Target);
  bool TryBeginSpecialAbilityTargeting(ESkaldAbilitySlot Slot);
  bool HandleAbilityTargetingInput(ESkaldAbilitySlot Slot);
  bool TryExecuteAbilityOnFighter(AFighterPawn *Source,
                                  ESkaldAbilitySlot Slot,
                                  AFighterPawn *Target, FText &OutError,
                                  bool bPerformAttack = true);
  bool TryExecuteAbilityAtCell(const FPendingAbilityCommand &Command,
                               const FIntPoint &Cell, FText &OutError);

  AFighterPawn *FindCellAbilityAttackTarget(const AFighterPawn *Source,
                                            const FIntPoint &Cell,
                                            int32 Radius) const;

  AFighterPawn *ResolveCellAbilityPrimaryTarget(
      const FPendingAbilityCommand &Command, const FIntPoint &Cell) const;
  void DetermineControlledBattleSide();
  void TryDispatchPendingAttackPresentationNotifications();
  void HandlePendingPresentationTimerTick();
  void EnsureDiceWidgets();
  FGuid TriggerAttackDicePresentation(AFighterPawn *Attacker,
                                      AFighterPawn *Defender,
                                      const FDiceRollResult &Result);
  FGuid TriggerInitiativeDicePresentation(int32 AttackerRoll,
                                          int32 DefenderRoll);
  void ShowInitiativeResults(int32 PlayerResult, int32 EnemyResult);
  void HideInitiativeResults();
  USkaldDiceManager *ResolveDiceManager();
  FLinearColor ResolveFactionColor(ESkaldFaction Faction);
  FLinearColor ResolveBattleFactionColor(bool bAttackerSide);

  void StartInitiativeDiceSequence(int32 AttackerRoll, int32 DefenderRoll);
  void HandleInitiativeDiceOverviewReached();
  void HandleInitiativeDiceCleanupFinished();
  void HandleInitiativeDiceReturnComplete();
  void ResetInitiativeDiceSequence();
  void CompletePendingInitiativeSequence();

  void StartAttackDiceSequence(AFighterPawn *Attacker, AFighterPawn *Defender,
                               const FDiceRollResult &Result);
  void ProcessAttackResolutionPresentation(AFighterPawn *Attacker,
                                           AFighterPawn *Defender,
                                           const FDiceRollResult &Result);
  void ResetAttackDiceSequence();
  void HandleAttackDiceOverviewReached();
  void HandleAttackDiceCleanupFinished();
  void HandleAttackDiceReturnComplete();
  void CompletePendingAttackSequence();
  bool ComputeBattlefieldOverviewTransform(float CurrentYaw,
                                           FVector &OutLocation,
                                           FRotator &OutRotation,
                                           float &OutZoom) const;
  void EnsureDiceManagerBindings();
  void RestoreStrategicInitiativeCamera();
  void CacheStrategicInitiativeRollId(const FGuid &RollId);

  UFUNCTION()
  void HandlePhysicalDiceRollCompleted(const FGuid &RollId,
                                       const TArray<int32> &Results);
  void ApplyPendingPhysicalAttackResults();
  UFUNCTION()
  void HandleDiceRollStarted(const FGuid &RollId);

  UPROPERTY()
  TObjectPtr<AFighterPawn> SelectedFighter;

  UPROPERTY()
  TObjectPtr<AFighterPawn> LockedActiveFighter;

  /** Cached state when targeting Veil Step destinations. */
  TOptional<ESkaldAbilitySlot> PendingVeilStepSlot;
  TWeakObjectPtr<AFighterPawn> PendingVeilStepFighter;
  TOptional<FPendingAbilityCommand> PendingAbilityCommand;

  /** Friendly fighters tracked for HUD list synchronization. */
  TSet<TWeakObjectPtr<AFighterPawn>> ObservedFriendlyFighters;

  /** Delegate handle used to watch for fighter spawns. */
  FDelegateHandle FighterSpawnedHandle;

  bool bControlsAttackerSide = false;
  bool bControlsDefenderSide = false;

  /** Cached copy of the last initiative value rolled locally so it can be
   *  re-presented once the server confirms the result. */
  int32 LastLocalInitiativeRoll = 0;
  int32 LastLocalInitiativeAttacker = INDEX_NONE;
  int32 LastLocalInitiativeDefender = INDEX_NONE;
  bool bInitiativeRollTriggeredLocally = false;
  FGuid ActiveLocalInitiativeRollId;

  /** Cached initiative value pending presentation on the strategic HUD. */
  int32 PendingStrategicInitiativeRoll = 0;

  /** Cached opponent initiative value for the strategic HUD overlay. */
  int32 PendingStrategicInitiativeEnemyRoll = 0;

  /** Round index associated with the pending strategic initiative roll. */
  int32 PendingStrategicInitiativeRound = 0;

  /** Whether the cached strategic initiative roll was the winning value. */
  bool bPendingStrategicInitiativeWin = false;

  FGuid PendingStrategicInitiativeRollId;
  bool bStrategicInitiativeCameraActive = false;

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

  /** Timer handle used to hide the initiative result widget after a delay. */
  FTimerHandle InitiativeResultHideTimer;

  /** State used to avoid replaying the grid turn cue when the prompt has not changed. */
  int32 LastBattleTurnSoundRound = INDEX_NONE;
  bool bLastBattleTurnSoundWasAttacker = false;
  int32 LastBattleTurnSoundAvailableCount = INDEX_NONE;

  /** Timer that polls for combat presentation completion (dice + floaters). */
  FTimerHandle BattlePresentationMonitorHandle;

  /** Timer used to retry showing a pending prepare-for-battle prompt. */
  FTimerHandle PendingReadyPromptRetryHandle;

  /** Timer that defers hiding the prepare prompt after an enemy retreat. */
  FTimerHandle EnemyRetreatHidePromptHandle;

  /** Number of pending presentation completions awaiting acknowledgment. */
  int32 PendingAttackPresentationNotifications = 0;

  /** Cached battle manager awaiting presentation completion notification. */
  TWeakObjectPtr<UGridBattleManager> PendingPresentationBattleManager;

  /** Pending dice feedback bookkeeping so FX can align with reveal timings. */
  TArray<FPendingDiceFeedbackState> PendingDiceFeedbackStates;

  /** Prevents duplicate initiative dice visuals per round. */
  bool bInitiativeRollPresentationShown = false;

  struct FPendingInitiativeDiceSequence
  {
    bool bActive = false;
    bool bHadBattleCamera = false;
    bool bOverviewPrimed = false;
    FVector OriginalLocation = FVector::ZeroVector;
    FRotator OriginalRotation = FRotator::ZeroRotator;
    float OriginalZoom = 0.f;
    TWeakObjectPtr<AActor> OriginalLockTarget;
    FGuid ActiveRollId;
    int32 AttackerResult = 0;
    int32 DefenderResult = 0;
    float OverviewStartTime = 0.f;
    float OverviewDuration = 0.f;
    FTimerHandle OverviewTimerHandle;
    FTimerHandle CleanupDelayHandle;
    FTimerHandle ReturnTimerHandle;
  };

  FPendingInitiativeDiceSequence PendingInitiativeSequence;

  void PrimeInitiativeDiceOverview();

  struct FPendingAttackDiceSequence
  {
    TWeakObjectPtr<AFighterPawn> Attacker;
    TWeakObjectPtr<AFighterPawn> Defender;
    FDiceRollResult Result;
    TArray<int32> PhysicalRollResults;
    bool bActive = false;
    bool bHadBattleCamera = false;
    bool bHasPhysicalResults = false;
    FVector OriginalLocation = FVector::ZeroVector;
    FRotator OriginalRotation = FRotator::ZeroRotator;
    float OriginalZoom = 0.f;
    TWeakObjectPtr<AActor> OriginalLockTarget;
    FGuid ActiveRollId;
    FFighterStats AttackerSnapshot;
    FFighterStats DefenderSnapshot;
    FTimerHandle OverviewTimerHandle;
    FTimerHandle CleanupDelayHandle;
    FTimerHandle ReturnTimerHandle;
  };

  FPendingAttackDiceSequence PendingAttackSequence;
  struct FPendingManualDiceSequence
  {
    TWeakObjectPtr<AFighterPawn> Attacker;
    TWeakObjectPtr<AFighterPawn> Defender;
    FDiceRollResult Result;
    bool bActive = false;
    bool bHadBattleCamera = false;
    bool bTriggerServerRoll = false;
    bool bAwaitingRollId = false;
    bool bAwaitingRollCompletion = false;
    bool bHasResult = false;
    bool bPendingResolutionDispatch = false;
    FVector OriginalLocation = FVector::ZeroVector;
    FRotator OriginalRotation = FRotator::ZeroRotator;
    float OriginalZoom = 0.f;
    TWeakObjectPtr<AActor> OriginalLockTarget;
    FGuid ActiveRollId;
    FTimerHandle OverviewTimerHandle;
    FTimerHandle CleanupDelayHandle;
    FTimerHandle ReturnTimerHandle;
  };

  void ResetManualDiceSequence();
  bool BeginManualDiceSequence(class AFighterPawn* Attacker);
  void HandleManualDiceOverviewReached();
  void HandleManualDiceCleanupFinished();
  void HandleManualDiceReturnComplete();
  void TryCompleteManualDiceSequence();

  FPendingManualDiceSequence PendingManualSequence;
  bool bDiceDelegatesBound = false;
};
