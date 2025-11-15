#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DiceRollConfig.h"
#include "TimerManager.h"
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
    FGuid RollDice_D6(int32 NumPlayerDice, int32 NumEnemyDice, bool bForInitiative,
        FLinearColor PlayerColor = FLinearColor::Transparent,
        FLinearColor EnemyColor = FLinearColor::Transparent,
        FGuid OverrideRollId = FGuid());

    UFUNCTION(BlueprintCallable, Category = "Dice")
    void SetConfig(UDiceRollConfig* InConfig);

    UFUNCTION(BlueprintCallable, Category = "Dice")
    TArray<int32> RollDiceBlocking_D6(int32 NumDice);

    /**
     * Hold initiative dice on screen until an explicit release call arrives.
     * While active, subsequent initiative rolls reuse the same arena so the
     * presentation stays continuous across multiple participants.
     */
    void SetHoldInitiativeDice(bool bHold);

    /** Release any initiative dice that are currently being held. */
    void ReleaseHeldInitiativeDice();

    /**
     * Schedule a dice roll presentation using precomputed results.
     *
     * @param PlayerResults Results that should be tinted for the local player.
     * @param EnemyResults Results that should be tinted for the opposing side.
     * @param bForInitiative Whether this scripted roll represents an initiative comparison.
     * @param DurationOverride Optional fixed duration for the presentation. Negative values use config timing.
     */
    UFUNCTION(BlueprintCallable, Category = "Dice")
    FGuid PlayScriptedRoll(const TArray<int32>& PlayerResults, const TArray<int32>& EnemyResults,
        bool bForInitiative, float DurationOverride = -1.f,
        FLinearColor PlayerColor = FLinearColor::Transparent,
        FLinearColor EnemyColor = FLinearColor::Transparent);

    /** Returns the duration before arena and dice are fully cleaned up. */
    float GetCleanupDelay() const;

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
        FLinearColor PlayerColor = FLinearColor::Transparent;
        FLinearColor EnemyColor = FLinearColor::Transparent;

        struct FDieState
        {
            TWeakObjectPtr<ASkaldDiceD6> Actor;
            int32 DesiredValue = INDEX_NONE;
            int32 ResolvedValue = INDEX_NONE;
            bool bIsPlayerDie = false;
            bool bSnapToDesiredValue = true;
        };

        TArray<FDieState> Dice;
    };

    FRandomStream DeterministicStream;
    bool bDeterministic = false;

    FActiveRoll& AddRoll(int32 PlayerDice, int32 EnemyDice, float Duration, const FGuid& RollId,
        bool bForInitiative, FLinearColor PlayerColor, FLinearColor EnemyColor);
    void CompleteRoll(FGuid RollId);
    void BroadcastInterim(FGuid RollId);
    bool SpawnPhysicalRoll(FActiveRoll& Roll, const TArray<int32>* PlayerResults = nullptr, const TArray<int32>* EnemyResults = nullptr);
    void CleanupRollActors(FActiveRoll& Roll);
    void QueueDeferredInitiativeCleanup(FActiveRoll& Roll);
    void FinalizeDeferredInitiativeCleanup();
    bool ShouldReuseInitiativeArena() const;
    ADiceRollArena* ResolveInitiativeArenaForRoll(FActiveRoll& Roll);

    UFUNCTION()
    void HandleDieSettled(ASkaldDiceD6* Dice, int32 FaceValue);

    TMap<FGuid, FActiveRoll> ActiveRolls;

    struct FDeferredInitiativeCleanup
    {
        TArray<TWeakObjectPtr<ASkaldDiceD6>> Dice;
        TWeakObjectPtr<ADiceRollArena> Arena;
        float DiceLifeSpan = 0.f;
        float ArenaLifeSpan = 0.f;
    };

    bool bHoldInitiativeDiceUntilRelease = false;
    TWeakObjectPtr<ADiceRollArena> SharedInitiativeArena;
    TMap<FGuid, FDeferredInitiativeCleanup> DeferredInitiativeCleanups;

    TArray<int32> GenerateResults(int32 TotalDice);
};
