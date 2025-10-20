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
class UAudioComponent;
class USoundAttenuation;
class USoundBase;
class USoundClass;

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

  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  bool bValid = false;
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
  /** Initialize the game instance. */
  virtual void Init() override;

  virtual void Shutdown() override;

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

  /** Address to join when acting as a client. */
  UPROPERTY(BlueprintReadWrite, Category = "Network")
  FString JoinAddress;

  /** Number of AI opponents requested by the player. */
  UPROPERTY(BlueprintReadWrite, Category = "Player")
  int32 AIPlayersToSpawn = 1;

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

  /** Deploy widget instance owned by the game instance for Slate insertion. */
  UPROPERTY(Transient)
  TObjectPtr<UUserWidget> DeployWidget = nullptr;

  /** Dice roll variants designers can assign for per-die reveals. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Battle")
  TArray<TObjectPtr<USoundBase>> DiceRollVariants;

  /** Attack preparation cues triggered as resolution begins. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Battle")
  TArray<TObjectPtr<USoundBase>> AttackPrepareCues;

  /** Attack resolution cues triggered whenever hits occur. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Battle")
  TArray<TObjectPtr<USoundBase>> AttackResolveCues;

  /** Additional cues to highlight critical successes. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Battle")
  TArray<TObjectPtr<USoundBase>> AttackCritCues;

  /** Optional attenuation settings shared by attack cues. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Battle")
  TObjectPtr<USoundAttenuation> AttackCueAttenuation = nullptr;

  /** Sound class used when spawning transient battle cues (defaults to master bus). */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Battle")
  TObjectPtr<USoundClass> MasterSoundClass = nullptr;

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

  /** Play a randomized dice roll sound for UI driven reveals. */
  UFUNCTION(BlueprintCallable, Category = "Audio|Battle")
  void PlayRandomDiceRollVariant(UObject *WorldContextObject) const;

  /** Fire an attack preparation cue at the provided world location. */
  UFUNCTION(BlueprintCallable, Category = "Audio|Battle")
  void PlayAttackPrepareCue(UObject *WorldContextObject,
                            const FVector &Location) const;

  /** Fire an attack resolution cue at the provided world location. */
  UFUNCTION(BlueprintCallable, Category = "Audio|Battle")
  void PlayAttackResolveCue(UObject *WorldContextObject,
                            const FVector &Location) const;

  /** Fire an attack critical cue at the provided world location. */
  UFUNCTION(BlueprintCallable, Category = "Audio|Battle")
  void PlayAttackCritCue(UObject *WorldContextObject,
                         const FVector &Location) const;

  /** Clear any cached return destination once travel has completed. */
  void ClearPendingReturnMap();

  /** Toggle the travel pending guard and log the change. */
  UFUNCTION(BlueprintCallable)
  void SetTravelPending(bool bInPending);

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

  /** Handle network failures and return to the lobby. */
  UFUNCTION()
  void HandleNetworkFailure(UWorld *World, UNetDriver *Driver,
                            ENetworkFailure::Type FailureType,
                            const FString &ErrorString);

  /** Return to the main menu and clear any in-progress session data. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Menu")
  void ReturnToMainMenu();

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

  void HandleWorldBeginPlay(UWorld *LoadedWorld);
  void RequestPendingBattleResolution(UWorld *LoadedWorld);
  void AttemptResolvePendingBattle(int32 Attempt);
  void ResetSessionState();

  FTimerHandle PendingBattleResolutionKickoffHandle;
};
