#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Skald_GameState.generated.h"

class ASkaldPlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSkaldPlayersUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSkaldTurnChanged, int32, NewTurnIndex);

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

    /** Broadcast whenever the player list changes. */
    UPROPERTY(BlueprintAssignable, Category="GameState|Events")
    FSkaldPlayersUpdated OnPlayersUpdated;

    /** Broadcast when the active turn index changes (client & server). */
    UPROPERTY(BlueprintAssignable, Category="GameState|Events")
    FSkaldTurnChanged OnTurnIndexChanged;

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

protected:
    UFUNCTION()
    void OnRep_Players();

    UFUNCTION()
    void OnRep_CurrentTurnIndex();

    /** Keep CurrentTurnIndex in bounds after roster changes. */
    void ClampTurnIndex();

    /** Keep Players unique & stably ordered by PlayerId. */
    void SortAndDedupPlayers();
};

