#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Skald_GameState.generated.h"

class ASkaldPlayerState;

/**
 * Stores information about players and the current turn.
 */
UCLASS()
class SKALD_API ASkaldGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    ASkaldGameState();

    /** List of players participating in the match. */
    UPROPERTY(BlueprintReadOnly)
    TArray<ASkaldPlayerState*> Players;

    /** Index of the player whose turn is active. */
    UPROPERTY(BlueprintReadOnly)
    int32 CurrentTurnIndex;

    void AddPlayerState(ASkaldPlayerState* PlayerState);
    ASkaldPlayerState* GetCurrentPlayer() const;
};

