#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Skald_TurnManager.generated.h"

class ASkaldPlayerController;

/**
 * Handles turn sequencing for all registered player controllers.
 */
UCLASS()
class SKALD_API ATurnManager : public AActor
{
    GENERATED_BODY()

public:
    ATurnManager();

    virtual void BeginPlay() override;

    void RegisterController(ASkaldPlayerController* Controller);
    void StartTurns();
    void AdvanceTurn();

protected:
    UPROPERTY()
    TArray<ASkaldPlayerController*> Controllers;

    int32 CurrentIndex;
};

