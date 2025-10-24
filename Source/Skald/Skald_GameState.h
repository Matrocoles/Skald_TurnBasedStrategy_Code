#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "GridBattleManager.h" // for FFighterDefinition (ensure it’s a USTRUCT)
#include "SkaldTypes.h"
#include "TimerManager.h"
#include "Skald_GameState.generated.h"

class ASkaldPlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSkaldPlayersUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSkaldTurnIndexChanged, int32, NewTurnIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFighterRosterUpdated);

UENUM(BlueprintType)
enum class EBattlePhase : uint8
{
    None,
    Deploy,
    // Additional phases can be added here as the flow expands
};

/**
 * Stores information about players and the current turn.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ASkaldGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    ASkaldGameState();

    /** List of players participating in the match. */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Players, Category="GameState")
    TArray<ASkaldPlayerState*> Players;

    UPROPERTY(ReplicatedUsing=OnRep_BattlePhase)
    EBattlePhase BattlePhase = EBattlePhase::None;

    /** Broadcast whenever the player list changes. */
    UPROPERTY(BlueprintAssignable, Category="GameState|Events")
    FSkaldPlayersUpdated OnPlayersUpdated;

    /** Broadcast when the active turn index changes (client & server). */
    UPROPERTY(BlueprintAssignable, Category="GameState|Events")
    FSkaldTurnIndexChanged OnTurnIndexChanged;

    /** Replicated roster of all fighters available in the lobby. */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_FighterRoster, Category="GameState|Fighters")
    TArray<FFighterDefinition> FighterRoster;

    /** Broadcast whenever FighterRoster replicates/changes. */
    UPROPERTY(BlueprintAssignable, Category="GameState|Events")
    FFighterRosterUpdated OnFighterRosterUpdated;

    /** Cached snapshot of the pending battle readiness state. */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_PendingBattleReady,
              Category="GameState|Battle")
    FSkaldBattleReadyState PendingBattleReadyState;

    // ---- Battle Summary (replicated) ----
    /** Winner of the last completed battle. */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_BattleSummary, Category="GameState|Battle")
    ESkaldFaction LastBattleWinner = ESkaldFaction::None;

    /** Total attacker casualties (army cost) from the last battle. */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_BattleSummary, Category="GameState|Battle")
    int32 LastAttackerCasualties = 0;

    /** Total defender casualties (army cost) from the last battle. */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_BattleSummary, Category="GameState|Battle")
    int32 LastDefenderCasualties = 0;

    /** Notifies clients when the summary changes. */
    UPROPERTY(BlueprintAssignable, Category="GameState|Events")
    FSkaldPlayersUpdated OnBattleSummaryUpdated;

    /** Index of the player whose turn is active. */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_CurrentTurnIndex, Category="GameState")
    int32 CurrentTurnIndex;

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    virtual void AddPlayerState(APlayerState* PlayerState) override;
    virtual void RemovePlayerState(APlayerState* PlayerState) override;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category="GameState")
    ASkaldPlayerState* GetCurrentPlayer() const;

    /** Retrieve a player state by its PlayerID. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="GameState")
    ASkaldPlayerState* GetPlayerById(int32 PlayerID) const;

    /** Server API to set/update roster. */
    UFUNCTION(Server, Reliable)
    void ServerSetFighterRoster(const TArray<FFighterDefinition>& InRoster);

    void SetBattlePhase(EBattlePhase NewPhase);

    /** Getter for BP/UI */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="GameState|Fighters")
    const TArray<FFighterDefinition>& GetFighterRoster() const { return FighterRoster; }

    /**
     * Manual trigger for broadcasting battle summary updates on the server.
     * Calls the OnRep handler so server-side UI can refresh immediately.
     */
    void NotifyBattleSummaryChanged();

    /** Server-only helper to update the replicated pending battle readiness. */
    void SetPendingBattleReady(const FSkaldBattleReadyState& NewState);

    /** Returns true when all human-controlled parties have readied up. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="GameState|Battle")
    bool AreAllRequiredPartiesReady() const;

    /** Accessor for replicated pending battle readiness state. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="GameState|Battle")
    const FSkaldBattleReadyState& GetPendingBattleReady() const
    {
        return PendingBattleReadyState;
    }

    /** Request a short-lived global slowdown for cinematic feedback. */
    void RequestTransientSlowdown(float TargetDilation, float DurationSeconds);

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
    UFUNCTION()
    void OnRep_Players();

    UFUNCTION()
    void OnRep_BattlePhase();

    UFUNCTION()
    void OnRep_CurrentTurnIndex();

    UFUNCTION()
    void OnRep_FighterRoster();

    UFUNCTION()
    void OnRep_BattleSummary();

    UFUNCTION()
    void OnRep_PendingBattleReady();

    /** Keep CurrentTurnIndex in bounds after roster changes. */
    void ClampTurnIndex();

    /** Keep Players unique & stably ordered by PlayerId. */
    void SortAndDedupPlayers();

    void HandleTimeDilationReset();

    FTimerHandle TimeDilationResetHandle;
    float OriginalTimeDilation = 1.f;
    float ActiveTimeDilation = 1.f;
    bool bTimeDilationRequestActive = false;
};

