#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Skald_GameMode.generated.h"

class ATurnManager;
class ASkaldGameState;
class ASkaldPlayerController;
class ASkaldPlayerState;
class AWorldMap;

/**
 * GameMode responsible for managing player login and spawning the turn manager.
 */
UCLASS()
class SKALD_API ASkaldGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ASkaldGameMode();

    virtual void BeginPlay() override;
    virtual void PostLogin(APlayerController* NewPlayer) override;

protected:
    /** Handles turn sequencing for the match. */
    UPROPERTY()
    ATurnManager* TurnManager;

    /** Holds all territory actors for the current map. */
    UPROPERTY()
    AWorldMap* WorldMap;
};

