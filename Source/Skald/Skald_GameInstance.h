#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "Engine/EngineBaseTypes.h"
#include "TimerManager.h"
#include "Engine/GameInstance.h"
#include "SkaldTypes.h"
#include "Templates/SharedPointer.h"
#include "Skald_GameInstance.generated.h"

class SWidget;

class UGridBattleManager;
class USkaldBattleLevelManager;
class UUserWidget;
class ASkald_BattleGameMode;
class USkaldSaveGame;
class UNetDriver;
class UWorld;
class ASkaldGameMode;
class AWorldMap;
class ATerritory;
class ASkaldGameState;
class ASkaldPlayerState;
class UTexture2D;
class UDiceRollConfig;
class USkaldDiceManager;

USTRUCT(BlueprintType)
struct FSkaldTravelState
{
  GENERATED_BODY()

  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  int32 ExpectedControllers = 0;

  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  TArray<int32> HumanOwnedTerritories;

  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  int32 AttackerTerritory = -1;

  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  int32 DefenderTerritory = -1;

  /** Canonical map to return to once battle travel completes. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  FString ReturnMap;

  /** Snapshot of the overworld territories captured prior to travel. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  TArray<FS_Territory> CachedTerritories;

  /** Cached player data captured alongside the travel snapshot. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  TArray<FS_PlayerData> PlayerSnapshots;

  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  bool bValid = false;
};

USTRUCT(BlueprintType)
struct FSkaldAIPlayerConfig
{
  GENERATED_BODY()

  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  FString DisplayName;

  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  ESkaldFaction Faction = ESkaldFaction::None;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSkaldFactionsUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSkaldBattleMapStateChanged,
                                            bool,
                                            bIsInBattleMap);
/** Game instance storing player selections from the lobby. */
UCLASS()
class SKALD_API USkaldGameInstance : public UGameInstance {
  GENERATED_BODY()

public:
  USkaldGameInstance();

  /** Initialize the game instance. */
  virtual void Init() override;

  virtual void Shutdown() override;

  /** Resolve the configured colour for a given faction. */
  UFUNCTION(BlueprintCallable, Category = "Player")
  FLinearColor GetFactionColor(ESkaldFaction InFaction) const;

  /** Default faction colours used when no override has been configured. */
  static FLinearColor GetDefaultFactionColor(ESkaldFaction InFaction);

  /** Player chosen display name. */
  UPROPERTY(BlueprintReadWrite, Category = "Player")
  FString DisplayName = TEXT("Player");

  /** Selected faction for this player. */
  UPROPERTY(BlueprintReadWrite, Category = "Player")
  ESkaldFaction Faction = ESkaldFaction::Human;

  /** Whether the game was started in multiplayer mode. */
  UPROPERTY(BlueprintReadWrite, Category = "Player")
  bool bIsMultiplayer = false;

  /** True if this instance is hosting a multiplayer session. */
  UPROPERTY(BlueprintReadWrite, Category = "Network")
  bool bIsHost = false;

  /** Enable verbose connection logging to trace ControlChannel closes. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network|Debug")
  bool bVerboseControlChannelLogging = false;

  /** Address to join when acting as a client. */
  UPROPERTY(BlueprintReadWrite, Category = "Network")
  FString JoinAddress;

  /** Number of AI opponents requested by the player. */
  UPROPERTY(BlueprintReadWrite, Category = "Player")
  int32 AIPlayersToSpawn = 0;

  /** Number of human players that locked in before leaving the lobby. */
  UPROPERTY(BlueprintReadWrite, Category = "Player")
  int32 ExpectedLobbyPlayerCount = 0;

  /** Cached lobby selections for human players while travelling to the overworld. */
  TArray<FS_PlayerData> PendingLobbyPlayers;

  /** Factions that have already been selected by players or AI. */
  UPROPERTY(BlueprintReadWrite, Category = "Player")
  TArray<ESkaldFaction> TakenFactions;

  /** Event fired when the taken faction list changes. */
  UPROPERTY(BlueprintAssignable, Category = "Player|Events")
  FSkaldFactionsUpdated OnFactionsUpdated;

  /** Event fired whenever the game enters or exits the streamed battle map. */
  UPROPERTY(BlueprintAssignable, Category = "Battle|Events")
  FSkaldBattleMapStateChanged OnBattleMapStateChanged;

  /** Payload describing the battle to resolve when returning from the battle
   * map. */
  UPROPERTY(BlueprintReadWrite, Category = "Battle")
  FS_BattlePayload PendingBattle;

  /** Canonical path to the map that should be reloaded after resolving the
   *  current battle. */
  UPROPERTY(BlueprintReadWrite, Category = "Battle")
  FString PendingReturnMap;

  /** Serialized results waiting to be applied on the overworld. */
  UPROPERTY(BlueprintReadWrite, Category = "Battle")
  FGridBattleResolution PendingBattleResolution;

  /** True when PendingBattleResolution contains unapplied data. */
  UPROPERTY(BlueprintReadWrite, Category = "Battle",
            meta = (ScriptName = "has_pending_battle_resolution"))
  bool bPendingBattleResolution = false;

  /** Runtime manager used to execute grid based battles. */
  UPROPERTY(BlueprintReadWrite, Category = "Battle")
  class UGridBattleManager *GridBattleManager = nullptr;

  /** Runtime helper responsible for streaming battle levels into the overworld. */
  UPROPERTY(Transient)
  TObjectPtr<USkaldBattleLevelManager> BattleLevelStreamingManager = nullptr;

  /** True when the game has travelled to a dedicated battle map. */
  UPROPERTY(BlueprintReadWrite, Category = "Battle")
  bool bIsInBattleMap = false;

  /** True while a ServerTravel call is in flight. */
  UPROPERTY(Transient)
  bool bTravelPending = false;

  /** Active battle game mode controlling the streamed combat scene. */
  UPROPERTY(Transient)
  TWeakObjectPtr<ASkald_BattleGameMode> ActiveBattleGameMode;

  /** Snapshot of the overworld territories captured before travelling. */
  UPROPERTY(BlueprintReadWrite, Category = "Battle")
  TArray<FS_Territory> CachedWorldMapTerritories;

  /** Snapshot captured immediately before travelling so the overworld can be
   *  restored when returning from a battle map. */
  UPROPERTY(BlueprintReadWrite, Category = "Battle")
  TArray<FS_Territory> PendingTravelTerritories;

  /** Random stream used for deterministic combat rolls. */
  UPROPERTY()
  FRandomStream CombatRandomStream;

  /** Index of the current player turn when travelling between maps. */
  UPROPERTY(BlueprintReadWrite, Category = "Turn")
  int32 SavedTurnIndex = 0;

  /** Identifier of the player whose turn was active prior to travelling. */
  UPROPERTY(BlueprintReadWrite, Category = "Turn")
  int32 SavedTurnPlayerId = 0;

  /** Phase of the turn cycle that was active before travelling. */
  UPROPERTY(BlueprintReadWrite, Category = "Turn")
  ETurnPhase SavedTurnPhase = ETurnPhase::Reinforcement;

  /** Flag indicating whether the turn manager should resume after travel. */
  UPROPERTY(BlueprintReadWrite, Category = "Turn")
  bool bResumeTurns = false;

  /** Widget class used when showing the deploy overlay via the viewport. */
  UPROPERTY(EditDefaultsOnly, Category = "UI")
  TSubclassOf<UUserWidget> DeployWidgetClass;

  /** Editor configurable palette mapping factions to their UI colours. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player")
  TMap<ESkaldFaction, FLinearColor> FactionColors;

  /** Dice roll configuration asset automatically supplied to the dice subsystem. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice")
  TSoftObjectPtr<UDiceRollConfig> DefaultDiceRollConfig;

  /** Automatically pushes DefaultDiceRollConfig into the dice subsystem at startup. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice")
  bool bAutoInitialiseDiceSubsystem = true;

  /** Runtime cached dice configuration that remains loaded for the subsystem. */
  UPROPERTY(Transient)
  TObjectPtr<UDiceRollConfig> LoadedDiceRollConfig = nullptr;

  /** Optional faction emblem textures keyed by faction enum. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
  TMap<ESkaldFaction, TSoftObjectPtr<UTexture2D>> FactionEmblemMap;

  /** AI players defined by the multiplayer lobby prior to match start. */
  UPROPERTY(BlueprintReadWrite, Category = "Lobby")
  TArray<FSkaldAIPlayerConfig> PendingLobbyAIPlayers;

  /** Active global status message broadcast to all HUDs. */
  UPROPERTY(Transient)
  FText ActiveStatusMessage;

  /** Duration that the active status message should remain visible (0 for indefinite). */
  UPROPERTY(Transient)
  float ActiveStatusMessageDuration = 0.f;

  /** True when the active status message should persist until explicitly cleared. */
  UPROPERTY(Transient)
  bool bStatusMessagePersistent = false;

  /** Pending status message queued for the next world activation. */
  UPROPERTY(Transient)
  FText PendingStatusMessage;

  /** Whether a pending status message should be applied when the next world begins play. */
  UPROPERTY(Transient)
  bool bHasPendingStatusMessage = false;

  /** Persist flag corresponding to PendingStatusMessage. */
  UPROPERTY(Transient)
  bool bPendingStatusPersistent = true;

  /** Resolve the configured emblem for a given faction, if one exists. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UI")
  TSoftObjectPtr<UTexture2D> GetFactionEmblem(ESkaldFaction InFaction) const;

  /** Deploy widget instance owned by the game instance for Slate insertion. */
  UPROPERTY(Transient)
  TObjectPtr<UUserWidget> DeployWidget = nullptr;

  UFUNCTION(BlueprintCallable)
  void SetTravelState(const FSkaldTravelState &InState);

  /** Cache the snapshot that should be used to rebuild the overworld after
   *  travelling back from a battle map. */
  void SetPendingTravelSnapshot(const TArray<FS_Territory> &Snapshot);

  /** Clear any pending travel snapshot once the overworld has been rebuilt. */
  void ClearPendingTravelSnapshot();

  /** Retrieve the pending travel snapshot, if one exists. */
  const TArray<FS_Territory> &GetPendingTravelSnapshot() const
  {
    return PendingTravelTerritories;
  }

  /** Store the canonical map path we should travel back to once combat
   *  concludes. */
  void SetPendingReturnMap(const FString &InReturnMap);

  /** Return the currently cached travel destination, if any. */
  const FString &GetPendingReturnMap() const { return PendingReturnMap; }

  /** Clear any cached return destination once travel has completed. */
  void ClearPendingReturnMap();

  /** Toggle the travel pending guard and log the change. */
  UFUNCTION(BlueprintCallable)
  void SetTravelPending(bool bInPending);

  /** Whether the game instance currently expects a travel to complete. */
  UFUNCTION(BlueprintCallable, BlueprintPure)
  bool IsTravelPending() const;

  /** Whether there is cached travel or battle state that needs to be
   *  resolved before returning to the lobby or tearing down UI. */
  bool HasPendingBattleTravelContext() const;

  /** Guard against returning to the lobby while a travel payload is queued. */
  bool ShouldBypassLobbyTransition() const;

  /** Display a global status message across all player HUDs. */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void ShowGlobalStatusMessage(const FText &Message, float DisplayDuration, bool bPersistUntilCleared);

  /** Hide any active global status message. */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HideGlobalStatusMessage();

  /** Retrieve the current active status message, if any. */
  FText GetActiveStatusMessage() const { return ActiveStatusMessage; }

  /** Retrieve the duration for the current active status message. */
  float GetActiveStatusMessageDuration() const { return ActiveStatusMessageDuration; }

  /** Whether the current active status message persists until cleared. */
  bool IsActiveStatusMessagePersistent() const { return bStatusMessagePersistent; }

  /** Queue a status message that should appear when the next world becomes active. */
  void QueuePendingStatusMessage(const FText &Message, bool bPersistUntilCleared = true);

  /** Consume cached lobby data for the specified player if available. */
  bool ConsumePendingLobbyPlayerData(int32 PlayerId,
                                     const FString &InDisplayName,
                                     FS_PlayerData &OutData);

  /** Capture the overworld state so it can be restored after travelling. */
  bool CacheWorldMapSnapshot(UWorld *InWorldContext = nullptr);

  /** Attempt to rebuild the overworld from the cached snapshot data. */
  bool RestoreWorldFromSnapshot(UWorld *InWorldContext = nullptr);

  USkaldBattleLevelManager *GetBattleLevelManager() const { return BattleLevelStreamingManager; }

  void SetActiveBattleGameMode(ASkald_BattleGameMode *InGameMode);

  UFUNCTION(BlueprintCallable, Category = "Battle")
  void SetBattleMapActive(bool bInBattleMap);

  ASkald_BattleGameMode *GetActiveBattleGameMode() const {
    return ActiveBattleGameMode.Get();
  }

  UFUNCTION(BlueprintCallable, BlueprintPure)
  const FSkaldTravelState &GetTravelState() const { return TravelState; }

  UFUNCTION(BlueprintCallable)
  void ShowDeployWidget();

  UFUNCTION(BlueprintCallable)
  void HideDeployWidget();

  /** Seed the combat random stream so all clients use the same sequence. */
  UFUNCTION(BlueprintCallable, Category = "Battle")
  void SeedCombatRandomStream(int32 Seed);

  /** Apply the configured dice roll data asset to the dice subsystem. */
  UFUNCTION(BlueprintCallable, Category = "Dice")
  void ApplyDiceRollConfig();

  /** Handle network failures and return to the lobby. */
  UFUNCTION()
  void HandleNetworkFailure(UWorld *World, UNetDriver *Driver,
                            ENetworkFailure::Type FailureType,
                            const FString &ErrorString);

  /** Explicit, host-gated multiplayer session shutdown entry point. */
  void EndMultiplayerSession(bool bHostInitiated, const FString &Reason,
                             bool bForceShutdown = false);

  /** Return to the main menu and clear any in-progress session data. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Menu")
  void ReturnToMainMenu();

  /** Reset lobby and travel state so a fresh session can be configured. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Menu")
  void ResetSessionData();

  /** Save game loaded when transitioning from the main menu. */
  UPROPERTY(BlueprintReadWrite, Category = "SaveGame")
  USkaldSaveGame *LoadedSaveGame = nullptr;

private:
  UPROPERTY()
  FSkaldTravelState TravelState;

  /** Loading overlay displayed while travelling between maps. */
  TSharedPtr<SWidget> TravelLoadingOverlay;

  /** Handle invoked when the owning world begins play after travel. */
  FDelegateHandle PostWorldBeginPlayHandle;

  bool ValidateSnapshotPlayers(
      const TArray<FS_Territory> &Snapshot,
      const TMap<int32, ASkaldPlayerState *> &PlayerStateById,
      TArray<int32> &OutMissingPlayerIds) const;
  void ScheduleSnapshotRetry(UWorld *World);
  void HandleCacheWorldMapSnapshotRetry();

  void HandleWorldBeginPlay(UWorld *LoadedWorld);
  void HandleDeferredTravelResume(UWorld *LoadedWorld);
  void InitialiseDiceManager();
  bool ShouldAttemptTravelResume() const;
  void ScheduleTravelResume(UWorld *World);
  void AttemptResumeAfterTravel();
  void RequestPendingBattleResolution(UWorld *LoadedWorld);
  void AttemptResolvePendingBattle(int32 Attempt);
  void EnableControlChannelDiagnostics() const;
  void ResetSessionState();

  TWeakObjectPtr<UWorld> PendingResumeWorld;
  FTimerHandle PendingResumeDelayHandle;
  FTimerHandle PendingResumeRetryHandle;

  FTimerHandle PendingBattleResolutionKickoffHandle;
  FTimerHandle TerritorySnapshotRetryHandle;
};
