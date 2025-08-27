#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Skald_GameState.generated.h"

class ASkaldPlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSkaldPlayersUpdated);

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
    UPROPERTY(BlueprintReadOnly, Replicated, Category="GameState")
    TArray<ASkaldPlayerState*> Players;

    /** Broadcast whenever the player list changes. */
    UPROPERTY(BlueprintAssignable, Category="GameState|Events")
    FSkaldPlayersUpdated OnPlayersUpdated;

    /** Index of the player whose turn is active. */
    UPROPERTY(BlueprintReadOnly, Replicated, Category="GameState")
    int32 CurrentTurnIndex;

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    virtual void AddPlayerState(APlayerState* PlayerState) override;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category="GameState")
    ASkaldPlayerState* GetCurrentPlayer() const;
};

