#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkaldTypes.h"
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
    void SortControllersByInitiative();

protected:
    UPROPERTY()
    TArray<ASkaldPlayerController*> Controllers;

    int32 CurrentIndex;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Turn")
    E_TurnPhase CurrentPhase = E_TurnPhase::Reinforcement;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Turn")
    FS_BattlePayload PendingBattle;
};

