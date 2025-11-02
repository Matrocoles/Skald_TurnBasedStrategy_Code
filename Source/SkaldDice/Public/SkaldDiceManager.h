#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DiceRollConfig.h"
#include "SkaldDiceManager.generated.h"

class ADiceRollArena;
class ASkaldDiceD6;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDiceRollStarted, const FGuid&, RollId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDiceInterimUpdate, const FGuid&, RollId, float, Elapsed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDiceRollCompleted, const FGuid&, RollId, const TArray<int32>&, Results);

UCLASS()
class SKALDDICE_API USkaldDiceManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UPROPERTY(BlueprintAssignable, Category = "Dice")
    FOnDiceRollStarted OnDiceRollStarted;

    UPROPERTY(BlueprintAssignable, Category = "Dice")
    FOnDiceInterimUpdate OnDiceInterimUpdate;

    UPROPERTY(BlueprintAssignable, Category = "Dice")
    FOnDiceRollCompleted OnDiceRollCompleted;

    UFUNCTION(BlueprintCallable, Category = "Dice")
    FGuid RollDice_D6(int32 NumPlayerDice, int32 NumEnemyDice, bool bForInitiative);

    UFUNCTION(BlueprintCallable, Category = "Dice")
    void SetConfig(UDiceRollConfig* InConfig);

    UFUNCTION(BlueprintCallable, Category = "Dice")
    TArray<int32> RollDiceBlocking_D6(int32 NumDice);

    /**
     * Schedule a dice roll presentation using precomputed results.
     *
     * @param PlayerResults Results that should be tinted for the local player.
     * @param EnemyResults Results that should be tinted for the opposing side.
     * @param bForInitiative Whether this scripted roll represents an initiative comparison.
     * @param DurationOverride Optional fixed duration for the presentation. Negative values use config timing.
     */
    UFUNCTION(BlueprintCallable, Category = "Dice")
    FGuid PlayScriptedRoll(const TArray<int32>& PlayerResults, const TArray<int32>& EnemyResults, bool bForInitiative, float DurationOverride = -1.f);

protected:
    UPROPERTY(Transient)
    TObjectPtr<UDiceRollConfig> Config;

private:
    struct FActiveRoll
    {
        FGuid RollId;
        int32 TotalDice = 0;
        int32 PlayerDice = 0;
        int32 EnemyDice = 0;
        float StartTime = 0.f;
        float Duration = 0.f;
        FTimerHandle CompletionTimerHandle;
        FTimerHandle UpdateTimerHandle;
        bool bUseScriptedResults = false;
        bool bIsInitiative = false;
        TArray<int32> ScriptedResults;
        TWeakObjectPtr<ADiceRollArena> Arena;

        struct FDieState
        {
            TWeakObjectPtr<ASkaldDiceD6> Actor;
            int32 DesiredValue = INDEX_NONE;
            int32 ResolvedValue = INDEX_NONE;
            bool bIsPlayerDie = false;
        };

        TArray<FDieState> Dice;
    };

    FRandomStream DeterministicStream;
    bool bDeterministic = false;

    FActiveRoll& AddRoll(int32 PlayerDice, int32 EnemyDice, float Duration, const FGuid& RollId, bool bForInitiative);
    void CompleteRoll(FGuid RollId);
    void BroadcastInterim(FGuid RollId);
    bool SpawnPhysicalRoll(FActiveRoll& Roll, const TArray<int32>* PlayerResults = nullptr, const TArray<int32>* EnemyResults = nullptr);
    void CleanupRollActors(FActiveRoll& Roll);

    UFUNCTION()
    void HandleDieSettled(ASkaldDiceD6* Dice, int32 FaceValue);

    TMap<FGuid, FActiveRoll> ActiveRolls;

    TArray<int32> GenerateResults(int32 TotalDice);
};
