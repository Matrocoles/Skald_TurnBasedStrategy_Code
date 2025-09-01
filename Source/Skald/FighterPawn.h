#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GridBattleManager.h"
#include "FighterPawn.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthChanged, int32, NewHealth);

/** Pawn representing a fighter in grid battles. */
UCLASS()
class SKALD_API AFighterPawn : public APawn
{
    GENERATED_BODY()

public:
    AFighterPawn();

    /** Prepare the fighter for its activation. */
    UFUNCTION(BlueprintCallable, Category="Fighter")
    void BeginActivation();

    /** Move to the specified grid cell if actions remain. */
    UFUNCTION(BlueprintCallable, Category="Fighter")
    void MoveToCell(FIntPoint TargetCell);

    /** Perform an attack against another fighter. */
    UFUNCTION(BlueprintCallable, Category="Fighter")
    void PerformAttack(AFighterPawn* Target);

    /** Check whether this fighter is still alive. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Fighter")
    bool IsAlive() const;

    /** Statistics describing this fighter. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Fighter")
    FFighterStats Stats;

    /** Actions remaining for the current activation. */
    UPROPERTY(BlueprintReadOnly, Category="Fighter")
    int32 ActionsRemaining;

    /** Mesh used to display the fighter. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    UStaticMeshComponent* DisplayMesh;

    /** Event broadcast when health changes. */
    UPROPERTY(BlueprintAssignable, Category="Fighter|Events")
    FOnHealthChanged OnHealthChanged;

private:
    /** Current cell occupied by the fighter. */
    FIntPoint CurrentCell;
};

