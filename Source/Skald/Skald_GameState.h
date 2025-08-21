#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Skald_GameState.generated.h"

class ASkaldPlayerState;

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
    UPROPERTY(BlueprintReadOnly, Category="GameState")
    TArray<ASkaldPlayerState*> Players;

    /** Index of the player whose turn is active. */
    UPROPERTY(BlueprintReadOnly, Category="GameState")
    int32 CurrentTurnIndex;

    UFUNCTION(BlueprintCallable, Category="GameState")
    void AddPlayerState(ASkaldPlayerState* PlayerState);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category="GameState")
    ASkaldPlayerState* GetCurrentPlayer() const;
};

