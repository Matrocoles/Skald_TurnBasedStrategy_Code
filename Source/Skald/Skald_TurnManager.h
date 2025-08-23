#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkaldTypes.h"
#include "Skald_TurnManager.generated.h"

class ASkaldPlayerController;

/**
 * Handles turn sequencing for all registered player controllers.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ATurnManager : public AActor
{
    GENERATED_BODY()

public:
    ATurnManager();

    virtual void BeginPlay() override;

    UFUNCTION(BlueprintCallable, Category="Turn")
    void RegisterController(ASkaldPlayerController* Controller);

    UFUNCTION(BlueprintCallable, Category="Turn")
    void StartTurns();

    UFUNCTION(BlueprintCallable, Category="Turn")
    void AdvanceTurn();

    UFUNCTION(BlueprintCallable, Category="Turn")
    void SortControllersByInitiative();

    /** Transition into the grid based battle mode using the provided payload. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void TriggerGridBattle(const FS_BattlePayload& Battle);

protected:
    UPROPERTY(BlueprintReadOnly, Category="Turn")
    TArray<ASkaldPlayerController*> Controllers;

    UPROPERTY(BlueprintReadOnly, Category="Turn")
    int32 CurrentIndex;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Turn")
    ETurnPhase CurrentPhase = ETurnPhase::Reinforcement;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Turn")
    FS_BattlePayload PendingBattle;
};

