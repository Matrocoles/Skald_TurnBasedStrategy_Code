#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkaldTypes.h"
#include "Skald_TurnManager.generated.h"

class ASkaldPlayerController;
class ASkaldPlayerState;

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

    /** Called when all reinforcements have been deployed to transition
     *  the active player into the attack phase. */
    UFUNCTION(BlueprintCallable, Category="Turn")
    void BeginAttackPhase();

    /** Move to the next phase in the turn sequence. */
    UFUNCTION(BlueprintCallable, Category="Turn")
    void AdvancePhase();

    /** Update all players' HUDs with the specified player's army pool. */
    UFUNCTION(BlueprintCallable, Category="Turn")
    void BroadcastArmyPool(class ASkaldPlayerState* ForPlayer);

    UFUNCTION(BlueprintCallable, Category="Turn")
    void SortControllersByInitiative();

    /** Transition into the grid based battle mode using the provided payload. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void TriggerGridBattle(const FS_BattlePayload& Battle);

    /** Access the controllers array in its current initiative order. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Turn")
    const TArray<ASkaldPlayerController*>& GetControllers() const { return Controllers; }

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

