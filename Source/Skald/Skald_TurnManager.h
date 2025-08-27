#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkaldTypes.h"
#include "Skald_TurnManager.generated.h"

class ASkaldPlayerController;
class ASkaldPlayerState;

// Broadcast whenever the overall world state changes so HUDs can refresh.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSkaldWorldStateChanged);

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

    /** Update all HUDs with the specified player's resources. */
    UFUNCTION(BlueprintCallable, Category="Turn")
    void BroadcastResources(class ASkaldPlayerState* ForPlayer);

    UFUNCTION(BlueprintCallable, Category="Turn")
    void SortControllersByInitiative();

    /** Transition into the grid based battle mode using the provided payload. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void TriggerGridBattle(const FS_BattlePayload& Battle);

    /** Apply the outcome of a completed grid battle to the world map. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void ResolveGridBattleResult();

    /** Multicast the results of a resolved battle to all clients. */
    UFUNCTION(NetMulticast, Reliable)
    void ClientBattleResolved(int32 WinningPlayerID, int32 AttackerCasualties,
                              int32 DefenderCasualties, int32 FromTerritoryID,
                              int32 TargetTerritoryID, int32 NewOwnerPlayerID,
                              int32 SourceArmy, int32 TargetArmy);

    /** Access the controllers array in its current initiative order. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Turn")
    const TArray<TWeakObjectPtr<ASkaldPlayerController>>& GetControllers() const { return Controllers; }

    /** Retrieve the current phase of play. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Turn")
    ETurnPhase GetCurrentPhase() const { return CurrentPhase; }

    /** Event fired when the world state has changed. */
    UPROPERTY(BlueprintAssignable, Category="Turn")
    FSkaldWorldStateChanged OnWorldStateChanged;

protected:
    UPROPERTY(BlueprintReadOnly, Category="Turn")
    TArray<TWeakObjectPtr<ASkaldPlayerController>> Controllers;

    UPROPERTY(BlueprintReadOnly, Category="Turn")
    int32 CurrentIndex;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Turn")
    ETurnPhase CurrentPhase = ETurnPhase::Reinforcement;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Turn")
    FS_BattlePayload PendingBattle;

    /** Notify controllers and HUDs of a phase change. */
    void BroadcastCurrentPhase();
};

