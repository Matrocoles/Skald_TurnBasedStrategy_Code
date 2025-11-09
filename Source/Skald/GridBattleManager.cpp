#include "GridBattleManager.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "FighterPawn.h"
#include "GridOverlayComponent.h"
#include "SkaldLogging.h"
#include "SkaldDiceManager.h"
#include "Skald_GameInstance.h"
#include "Skald_GameState.h"
#include "Skald_PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "UObject/UnrealType.h"
#include "Skald_PlayerController.h"
#include "UI/BattleHUDWidget.h"
#include "Kismet/GameplayStatics.h"

namespace
{
constexpr float BattleConclusionBroadcastDelaySeconds = 1.5f;
// Delay used when automatically triggering a manual attack roll for AI controlled
// fighters so their flow aligns with the player camera presentation cadence.
constexpr float AutoManualAttackRollDelaySeconds = 0.65f;

FString DescribeFighter(const AFighterPawn* Fighter)
{
    if (!Fighter)
    {
        return TEXT("<None>");
    }

    const FString DisplayName = Fighter->GetHumanReadableName();
    return FString::Printf(TEXT("%s (%s | Attacker=%s | Activated=%s | Alive=%s)"),
        *GetNameSafe(Fighter),
        DisplayName.IsEmpty() ? TEXT("Unnamed") : *DisplayName,
        Fighter->bIsAttacker ? TEXT("true") : TEXT("false"),
        Fighter->HasActivatedThisRound() ? TEXT("true") : TEXT("false"),
        Fighter->IsAlive() ? TEXT("true") : TEXT("false"));
}

void GatherOwningPlayerControllers(UWorld* World, AFighterPawn* Fighter,
                                   TArray<ASkaldPlayerController*>& OutControllers)
{
    if (!World)
    {
        return;
    }

    if (Fighter)
    {
        if (ASkaldPlayerController* DirectController = Cast<ASkaldPlayerController>(Fighter->GetController()))
        {
            const ASkaldPlayerState* PlayerState = DirectController->GetPlayerState<ASkaldPlayerState>();
            if (!PlayerState || !PlayerState->bIsAI)
            {
                OutControllers.AddUnique(DirectController);
            }
        }
    }

    const bool bHasFaction = Fighter && Fighter->Faction != ESkaldFaction::None;

    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        ASkaldPlayerController* Candidate = Cast<ASkaldPlayerController>(*It);
        if (!Candidate || OutControllers.Contains(Candidate))
        {
            continue;
        }

        const ASkaldPlayerState* PlayerState = Candidate->GetPlayerState<ASkaldPlayerState>();
        if (!PlayerState || PlayerState->bIsAI)
        {
            continue;
        }

        if (!bHasFaction || PlayerState->Faction == Fighter->Faction)
        {
            OutControllers.Add(Candidate);
        }
    }
}

int32 CountFighters(const TArray<AFighterPawn*>& Fighters, bool bAttackerOnly)
{
    int32 Count = 0;
    for (const AFighterPawn* Fighter : Fighters)
    {
        if (!Fighter)
        {
            continue;
        }

        if (!Fighter->IsAlive())
        {
            continue;
        }

        if (Fighter->bIsAttacker != bAttackerOnly)
        {
            continue;
        }

        ++Count;
    }
    return Count;
}

bool TeamHasLivingFaction(const TArray<FFighter>& Fighters, ESkaldFaction Faction)
{
    if (Faction == ESkaldFaction::None)
    {
        return false;
    }

    for (const FFighter& Fighter : Fighters)
    {
        if (Fighter.Faction == Faction && Fighter.Stats.Health > 0)
        {
            return true;
        }
    }

    return false;
}
} // namespace

UGridBattleManager::UGridBattleManager()
{
    // FighterDefinitions is provided via editor (EditDefaultsOnly)
}

void UGridBattleManager::BeginDestroy()
{
    if (USkaldDiceManager* Manager = DiceManager.Get())
    {
        Manager->OnDiceRollCompleted.RemoveDynamic(this, &UGridBattleManager::HandleDiceRollCompleted);
    }
    DiceManager.Reset();

    if (UWorld* World = GetWorld())
    {
        for (TPair<TWeakObjectPtr<AFighterPawn>, FTimerHandle>& Pair : PendingAutoManualAttackRolls)
        {
            World->GetTimerManager().ClearTimer(Pair.Value);
        }
    }
    PendingAutoManualAttackRolls.Empty();

    Super::BeginDestroy();
}

void UGridBattleManager::SetRandomSeed(int32 Seed)
{
    Rng.Initialize(Seed);
}

void UGridBattleManager::InitBattle(const TArray<FFighter>& Attackers, const TArray<FFighter>& Defenders)
{
    AttackerTeam = Attackers;
    DefenderTeam = Defenders;
    CurrentRound = 0;
    InitiativeOrder.Empty();
    ActiveFighter = nullptr;
    CurrentTurn = 0;
    InitiativeWinnerFaction = ESkaldFaction::None;
    bIsAttackerTurn = true;
    AttackerInitialArmyCost = 0;
    for (const FFighter& Fighter : AttackerTeam)
    {
        if (Fighter.Stats.Health > 0)
        {
            AttackerInitialArmyCost += Fighter.Stats.ArmyCost;
        }
    }

    DefenderInitialArmyCost = 0;
    for (const FFighter& Fighter : DefenderTeam)
    {
        if (Fighter.Stats.Health > 0)
        {
            DefenderInitialArmyCost += Fighter.Stats.ArmyCost;
        }
    }

    AttackerSurvivorUnitCount = 0;
    DefenderSurvivorUnitCount = 0;
    AttackerSurvivorArmyCost = 0;
    DefenderSurvivorArmyCost = 0;
    bTeamsAssigned = false;
    bBattleConcluded = false;
    bAwaitingInitiativeRoll = false;
    LastInitiativeRollAttacker = 0;
    LastInitiativeRollDefender = 0;

    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Initialised battle: Attackers=%d, Defenders=%d"), AttackerTeam.Num(), DefenderTeam.Num());

    PendingAttackPresentationCount = 0;
    DeferredPresentationFinishes.Reset();
    bRoundStartDeferred = false;
}

FDiceRollResult UGridBattleManager::ResolveAttackDice(const FFighterStats& AttackerStats, const FFighterStats& DefenderStats, FRandomStream& RandomStream, const TArray<int32>& OverrideRolls)
{
    FDiceRollResult Result;
    Result.StartingHealth = DefenderStats.Health;
    Result.EndingHealth = DefenderStats.Health;

    if (AttackerStats.AttackDice <= 0 || Result.StartingHealth <= 0)
    {
        return Result;
    }

    const int32 DiceToRoll = FMath::Max(0, AttackerStats.AttackDice);
    if (DiceToRoll <= 0)
    {
        return Result;
    }

    TArray<int32> RollValues;
    RollValues.Reserve(DiceToRoll);

    if (OverrideRolls.Num() > 0)
    {
        RollValues = OverrideRolls;
        for (int32& Value : RollValues)
        {
            Value = FMath::Clamp(Value, 1, 6);
        }

        if (RollValues.Num() > DiceToRoll)
        {
            RollValues.SetNum(DiceToRoll);
        }
        else if (RollValues.Num() < DiceToRoll)
        {
            const int32 MissingCount = DiceToRoll - RollValues.Num();
            for (int32 Index = 0; Index < MissingCount; ++Index)
            {
                RollValues.Add(RandomStream.RandRange(1, 6));
            }
        }
    }
    else
    {
        for (int32 DieIndex = 0; DieIndex < DiceToRoll; ++DieIndex)
        {
            RollValues.Add(RandomStream.RandRange(1, 6));
        }
    }

    Result.DiceOutcomes.Reset();
    Result.DiceOutcomes.SetNum(DiceToRoll);

    const bool bHasOverrideRolls = OverrideRolls.Num() > 0;
    if (bHasOverrideRolls)
    {
        const int32 ValuesToStore = FMath::Min(DiceToRoll, RollValues.Num());
        for (int32 Index = 0; Index < ValuesToStore; ++Index)
        {
            Result.DiceOutcomes[Index].RollValue = FMath::Clamp(RollValues[Index], 1, 6);
        }
    }
    else
    {
        AFighterPawn::ApplyPhysicalRollResults(Result, RollValues, AttackerStats, DefenderStats);
    }
    return Result;
}

bool UGridBattleManager::ResolveAttack(FFighter& Attacker, FFighter& Defender, int32& OutDamage, FRandomStream& RandomStream, FDiceRollResult& OutResult)
{
    const int32 StartingHealth = Defender.Stats.Health;

    FDiceRollResult Result = ResolveAttackDice(Attacker.Stats, Defender.Stats, RandomStream);
    Defender.Stats.Health = FMath::Max(0, Result.EndingHealth);

    OutDamage = FMath::Clamp(Result.TotalDamage, 0, StartingHealth);
    if (Result.bHighStakesCritical)
    {
        Result.HighStakesFaction = Attacker.Faction;
    }
    OutResult = Result;

    return Result.EndingHealth > 0;
}

int32 UGridBattleManager::GetAttackerSurvivors()
{
    AttackerSurvivorUnitCount = 0;
    for (const FFighter& Fighter : AttackerTeam)
    {
        if (Fighter.Stats.Health > 0)
        {
            ++AttackerSurvivorUnitCount;
        }
    }
    return AttackerSurvivorUnitCount;
}

int32 UGridBattleManager::GetDefenderSurvivors()
{
    DefenderSurvivorUnitCount = 0;
    for (const FFighter& Fighter : DefenderTeam)
    {
        if (Fighter.Stats.Health > 0)
        {
            ++DefenderSurvivorUnitCount;
        }
    }
    return DefenderSurvivorUnitCount;
}

int32 UGridBattleManager::GetAttackerSurvivorCost() const
{
    return AttackerSurvivorArmyCost;
}

int32 UGridBattleManager::GetDefenderSurvivorCost() const
{
    return DefenderSurvivorArmyCost;
}

bool UGridBattleManager::IsSideControlledByAI(bool bForAttackers) const
{
    return IsSideAIControlled(bForAttackers);
}

AFighterPawn* UGridBattleManager::GetActiveFighter() const
{
    return ActiveFighter;
}

void UGridBattleManager::RegisterFighter(AFighterPawn* Fighter, bool bAsAttacker)
{
    if (!Fighter)
    {
        return;
    }

    Fighter->bIsAttacker = bAsAttacker;
    Fighter->ResetActivationState();

    if (!InitiativeOrder.Contains(Fighter))
    {
        InitiativeOrder.Add(Fighter);
    }
}

void UGridBattleManager::UnregisterFighter(AFighterPawn* Fighter)
{
    if (!Fighter)
    {
        return;
    }

    const bool bWasAttacker = Fighter->bIsAttacker;
    InitiativeOrder.Remove(Fighter);
    if (ActiveFighter == Fighter)
    {
        ActiveFighter = nullptr;
        OnActiveFighterChanged.Broadcast(nullptr);
    }

    EvaluateRoundProgress(bWasAttacker);
}

TArray<AFighterPawn*> UGridBattleManager::GetInitiativeOrderSnapshot() const
{
    TArray<AFighterPawn*> Snapshot;
    Snapshot.Reserve(InitiativeOrder.Num());

    for (AFighterPawn* Fighter : InitiativeOrder)
    {
        if (IsValid(Fighter))
        {
            Snapshot.Add(Fighter);
        }
    }

    return Snapshot;
}

TArray<FFighterDefinition> UGridBattleManager::GetFightersForFaction(ESkaldFaction Faction) const
{
    TArray<FFighterDefinition> Out;
    if (!FighterDefinitions) return Out;

    FighterDefinitions->ForeachRow<FFighterDefinition>(
        TEXT("GetFightersForFaction"),
        [&](const FName, const FFighterDefinition& Row)
        {
            if (Row.Faction == Faction)
            {
                FFighterDefinition Definition = Row;
                Definition.PassiveAbility = GetFactionPassive(Faction);
                Definition.ActiveAbility = GetFactionActiveAbility(Faction, Row.Stats.ArmyCost);
                Out.Add(MoveTemp(Definition));
            }
        });
    return Out;
}

bool UGridBattleManager::RollInitiative()
{
    if (bBattleConcluded)
    {
        return true;
    }

    if (!bAttackerInitiativeRollRequested || !bDefenderInitiativeRollRequested)
    {
        if (bAwaitingInitiativeRoll)
        {
            return false;
        }

        bAttackerInitiativeRollRequested = true;
        bDefenderInitiativeRollRequested = true;
    }

    LastInitiativeRollAttacker = 0;
    LastInitiativeRollDefender = 0;

    const bool bAttackersPresent = HasLivingFighters(true);
    const bool bDefendersPresent = HasLivingFighters(false);

    if (!bAttackersPresent && !bDefendersPresent)
    {
        InitiativeWinnerFaction = ESkaldFaction::None;
        bIsAttackerTurn = true;
        CompleteInitiativeRoll(0, 0);
        return true;
    }

    if (!bAttackersPresent)
    {
        InitiativeWinnerFaction = DefenderTeam.Num() > 0 ? DefenderTeam[0].Faction : ESkaldFaction::None;
        bIsAttackerTurn = false;
        CompleteInitiativeRoll(0, 0);
        return true;
    }

    if (!bDefendersPresent)
    {
        InitiativeWinnerFaction = AttackerTeam.Num() > 0 ? AttackerTeam[0].Faction : ESkaldFaction::None;
        bIsAttackerTurn = true;
        CompleteInitiativeRoll(0, 0);
        return true;
    }

    int32 AttackerRoll = PendingInitiativeRollAttacker.IsSet() ? FMath::Clamp(PendingInitiativeRollAttacker.GetValue(), 1, InitiativeDiceSides) : 0;
    int32 DefenderRoll = PendingInitiativeRollDefender.IsSet() ? FMath::Clamp(PendingInitiativeRollDefender.GetValue(), 1, InitiativeDiceSides) : 0;

    const bool bAttackerRollProvided = PendingInitiativeRollAttacker.IsSet();
    const bool bDefenderRollProvided = PendingInitiativeRollDefender.IsSet();
    const bool bNeedsAttackerRoll = !bAttackerRollProvided && AttackerRoll <= 0;
    const bool bNeedsDefenderRoll = !bDefenderRollProvided && DefenderRoll <= 0;

    if ((bNeedsAttackerRoll || bNeedsDefenderRoll))
    {
        if (bInitiativeRollAwaitingResults)
        {
            return false;
        }

        if (USkaldDiceManager* Manager = ResolveDiceManager())
        {
            PendingInitiativePlayerDice = bNeedsAttackerRoll ? 1 : 0;
            PendingInitiativeEnemyDice = bNeedsDefenderRoll ? 1 : 0;

            if (PendingInitiativePlayerDice > 0 || PendingInitiativeEnemyDice > 0)
            {
                const FGuid RollId = Manager->RollDice_D6(PendingInitiativePlayerDice, PendingInitiativeEnemyDice, true);
                if (RollId.IsValid())
                {
                    ActiveInitiativeRollId = RollId;
                    bInitiativeRollAwaitingResults = true;
                    return false;
                }

                PendingInitiativePlayerDice = 0;
                PendingInitiativeEnemyDice = 0;
            }
        }

        if (bNeedsAttackerRoll)
        {
            AttackerRoll = Rng.RandRange(1, InitiativeDiceSides);
        }

        if (bNeedsDefenderRoll)
        {
            DefenderRoll = Rng.RandRange(1, InitiativeDiceSides);
        }
    }

    if (!bAttackerRollProvided || !bDefenderRollProvided)
    {
        int32 Attempts = 0;
        const int32 MaxAttempts = 100;

        while (Attempts < MaxAttempts && (!bAttackerRollProvided || !bDefenderRollProvided))
        {
            if (!bAttackerRollProvided && AttackerRoll <= 0)
            {
                AttackerRoll = Rng.RandRange(1, InitiativeDiceSides);
            }

            if (!bDefenderRollProvided && DefenderRoll <= 0)
            {
                DefenderRoll = Rng.RandRange(1, InitiativeDiceSides);
            }

            if (AttackerRoll != DefenderRoll)
            {
                break;
            }

            if (!bAttackerRollProvided)
            {
                AttackerRoll = 0;
            }

            if (!bDefenderRollProvided)
            {
                DefenderRoll = 0;
            }

            ++Attempts;
        }

        if (!bAttackerRollProvided && AttackerRoll <= 0)
        {
            AttackerRoll = Rng.RandRange(1, InitiativeDiceSides);
        }

        if (!bDefenderRollProvided && DefenderRoll <= 0)
        {
            DefenderRoll = Rng.RandRange(1, InitiativeDiceSides);
        }
    }

    const bool bAttackerHasEmpireDiscipline = TeamHasLivingFaction(AttackerTeam, ESkaldFaction::Empire);
    const bool bDefenderHasEmpireDiscipline = TeamHasLivingFaction(DefenderTeam, ESkaldFaction::Empire);
    ApplyInitiativeAdjustments(AttackerRoll, DefenderRoll, bAttackerRollProvided, bDefenderRollProvided, bAttackerHasEmpireDiscipline, bDefenderHasEmpireDiscipline);

    CompleteInitiativeRoll(AttackerRoll, DefenderRoll);
    return true;
}

void UGridBattleManager::ApplyInitiativeAdjustments(int32& AttackerRoll, int32& DefenderRoll, bool bAttackerRollProvided, bool bDefenderRollProvided, bool bAttackerHasEmpireDiscipline, bool bDefenderHasEmpireDiscipline)
{
    if (bAttackerHasEmpireDiscipline && !bAttackerRollProvided)
    {
        const int32 Reroll = Rng.RandRange(1, InitiativeDiceSides);
        AttackerRoll = FMath::Max(AttackerRoll, Reroll);
    }

    if (bDefenderHasEmpireDiscipline && !bDefenderRollProvided)
    {
        const int32 Reroll = Rng.RandRange(1, InitiativeDiceSides);
        DefenderRoll = FMath::Max(DefenderRoll, Reroll);
    }

    if (AttackerRoll == DefenderRoll)
    {
        if (bAttackerRollProvided && !bDefenderRollProvided)
        {
            DefenderRoll = (DefenderRoll % InitiativeDiceSides) + 1;
        }
        else if (!bAttackerRollProvided && bDefenderRollProvided)
        {
            AttackerRoll = (AttackerRoll % InitiativeDiceSides) + 1;
        }
        else
        {
            AttackerRoll = InitiativeDiceSides;
            if (DefenderRoll == AttackerRoll)
            {
                DefenderRoll = FMath::Max(1, AttackerRoll - 1);
            }
        }
    }
}

void UGridBattleManager::CompleteInitiativeRoll(int32 AttackerRoll, int32 DefenderRoll)
{
    PendingInitiativeRollAttacker.Reset();
    PendingInitiativeRollDefender.Reset();
    bAttackerInitiativeRollRequested = false;
    bDefenderInitiativeRollRequested = false;

    LastInitiativeRollAttacker = AttackerRoll;
    LastInitiativeRollDefender = DefenderRoll;

    if (AttackerRoll >= DefenderRoll)
    {
        bIsAttackerTurn = true;
        InitiativeWinnerFaction = AttackerTeam.Num() > 0 ? AttackerTeam[0].Faction : ESkaldFaction::None;
    }
    else
    {
        bIsAttackerTurn = false;
        InitiativeWinnerFaction = DefenderTeam.Num() > 0 ? DefenderTeam[0].Faction : ESkaldFaction::None;
    }

    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Round %d initiative winner: %s (Attacker=%d Defender=%d)"), CurrentRound,
        *UEnum::GetValueAsString(InitiativeWinnerFaction), LastInitiativeRollAttacker, LastInitiativeRollDefender);

    const bool bHasPresentationListeners = OnInitiativeRollCompleted.IsBound();
    if (bHasPresentationListeners)
    {
        OnInitiativeRollCompleted.Broadcast(CurrentRound, LastInitiativeRollAttacker, LastInitiativeRollDefender, InitiativeWinnerFaction);
    }

    ScheduleRoundStart(bHasPresentationListeners);
}

USkaldDiceManager* UGridBattleManager::ResolveDiceManager()
{
    if (USkaldDiceManager* Manager = DiceManager.Get())
    {
        return Manager;
    }

    if (UWorld* World = GetWorld())
    {
        if (USkaldGameInstance* GameInstance = Cast<USkaldGameInstance>(World->GetGameInstance()))
        {
            if (USkaldDiceManager* Manager = GameInstance->GetSubsystem<USkaldDiceManager>())
            {
                if (!Manager->OnDiceRollCompleted.IsAlreadyBound(this, &UGridBattleManager::HandleDiceRollCompleted))
                {
                    Manager->OnDiceRollCompleted.AddDynamic(this, &UGridBattleManager::HandleDiceRollCompleted);
                }
                DiceManager = Manager;
                return Manager;
            }
        }
    }

    return nullptr;
}

void UGridBattleManager::HandleDiceRollCompleted(const FGuid& RollId, const TArray<int32>& Results)
{
    if (!bInitiativeRollAwaitingResults || RollId != ActiveInitiativeRollId)
    {
        return;
    }

    ActiveInitiativeRollId.Invalidate();
    bInitiativeRollAwaitingResults = false;

    const bool bAttackerRollProvided = PendingInitiativeRollAttacker.IsSet();
    const bool bDefenderRollProvided = PendingInitiativeRollDefender.IsSet();

    int32 AttackerRoll = bAttackerRollProvided ? FMath::Clamp(PendingInitiativeRollAttacker.GetValue(), 1, InitiativeDiceSides) : 0;
    int32 DefenderRoll = bDefenderRollProvided ? FMath::Clamp(PendingInitiativeRollDefender.GetValue(), 1, InitiativeDiceSides) : 0;

    int32 ResultIndex = 0;
    if (!bAttackerRollProvided && PendingInitiativePlayerDice > 0 && Results.IsValidIndex(ResultIndex))
    {
        AttackerRoll = FMath::Clamp(Results[ResultIndex++], 1, InitiativeDiceSides);
    }

    if (!bDefenderRollProvided && PendingInitiativeEnemyDice > 0 && Results.IsValidIndex(ResultIndex))
    {
        DefenderRoll = FMath::Clamp(Results[ResultIndex++], 1, InitiativeDiceSides);
    }

    PendingInitiativePlayerDice = 0;
    PendingInitiativeEnemyDice = 0;

    if (AttackerRoll <= 0)
    {
        AttackerRoll = Rng.RandRange(1, InitiativeDiceSides);
    }

    if (DefenderRoll <= 0)
    {
        DefenderRoll = Rng.RandRange(1, InitiativeDiceSides);
    }

    const bool bAttackerHasEmpireDiscipline = TeamHasLivingFaction(AttackerTeam, ESkaldFaction::Empire);
    const bool bDefenderHasEmpireDiscipline = TeamHasLivingFaction(DefenderTeam, ESkaldFaction::Empire);

    ApplyInitiativeAdjustments(AttackerRoll, DefenderRoll, bAttackerRollProvided, bDefenderRollProvided, bAttackerHasEmpireDiscipline, bDefenderHasEmpireDiscipline);

    float CleanupDelay = 0.f;
    if (USkaldDiceManager* Manager = ResolveDiceManager())
    {
        CleanupDelay = Manager->GetCleanupDelay();
    }

    if (UWorld* World = GetWorld())
    {
        FTimerManager& TimerManager = World->GetTimerManager();
        TimerManager.ClearTimer(InitiativeWinnerAnnouncementTimer);

        if (CleanupDelay > 0.f)
        {
            TimerManager.SetTimer(InitiativeWinnerAnnouncementTimer, FTimerDelegate::CreateUObject(this, &UGridBattleManager::CompleteInitiativeRoll, AttackerRoll, DefenderRoll), CleanupDelay, false);
            return;
        }
    }

    CompleteInitiativeRoll(AttackerRoll, DefenderRoll);
}

void UGridBattleManager::StartRound()
{
    if (bBattleConcluded)
    {
        return;
    }

    if (IsAwaitingAttackPresentation())
    {
        bRoundStartDeferred = true;
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] StartRound deferred while attack presentation is active."));
        return;
    }

    bRoundStartDeferred = false;

    bTeamsAssigned = true;

    ClearInactiveFighters();

    if (!HasLivingFighters(true) || !HasLivingFighters(false))
    {
        EndBattle();
        return;
    }

    ++CurrentRound;

    const int32 LivingAttackers = CountFighters(InitiativeOrder, true);
    const int32 LivingDefenders = CountFighters(InitiativeOrder, false);
    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Starting round %d (Attackers=%d, Defenders=%d)"), CurrentRound, LivingAttackers, LivingDefenders);

    ActiveFighter = nullptr;
    CurrentTurn = 0;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(InitiativePresentationTimer);
        World->GetTimerManager().ClearTimer(InitiativeAIRollTimer);
    }

    bAttackerInitiativeRollRequested = false;
    bDefenderInitiativeRollRequested = false;
    PendingInitiativeRollAttacker.Reset();
    PendingInitiativeRollDefender.Reset();
    bAwaitingInitiativeRoll = false;

    if (ShouldPauseForInitiativePrompt())
    {
        bAwaitingInitiativeRoll = true;
        OnActiveFighterChanged.Broadcast(nullptr);
        OnInitiativePhaseStarted.Broadcast(CurrentRound);
        return;
    }

    bAttackerInitiativeRollRequested = true;
    bDefenderInitiativeRollRequested = true;
    ResolveInitiativeRollInternal();
}

void UGridBattleManager::ConfirmInitiativeRoll(int32 AttackerRoll, int32 DefenderRoll)
{
    if (!bAwaitingInitiativeRoll || bBattleConcluded)
    {
        return;
    }

    if (AttackerRoll >= 0)
    {
        bAttackerInitiativeRollRequested = true;
        if (AttackerRoll > 0)
        {
            PendingInitiativeRollAttacker = AttackerRoll;
        }
    }

    if (DefenderRoll >= 0)
    {
        bDefenderInitiativeRollRequested = true;
        if (DefenderRoll > 0)
        {
            PendingInitiativeRollDefender = DefenderRoll;
        }
    }

    if (bAttackerInitiativeRollRequested && bDefenderInitiativeRollRequested)
    {
        ResolveInitiativeRollInternal();
        return;
    }

    ScheduleAIRollIfNeeded();
}

void UGridBattleManager::ResolveInitiativeRollInternal()
{
    if (!bAttackerInitiativeRollRequested || !bDefenderInitiativeRollRequested)
    {
        return;
    }

    bAwaitingInitiativeRoll = false;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(InitiativeAIRollTimer);
        World->GetTimerManager().ClearTimer(InitiativeWinnerAnnouncementTimer);
    }

    if (!RollInitiative())
    {
        // Awaiting physical dice results; finalization occurs in HandleDiceRollCompleted.
        return;
    }
}

void UGridBattleManager::ScheduleAIRollIfNeeded()
{
    if (bBattleConcluded || !bAwaitingInitiativeRoll)
    {
        return;
    }

    const bool bNeedsAttackerRoll = !bAttackerInitiativeRollRequested && IsSideAIControlled(true);
    const bool bNeedsDefenderRoll = !bDefenderInitiativeRollRequested && IsSideAIControlled(false);

    if (!bNeedsAttackerRoll && !bNeedsDefenderRoll)
    {
        return;
    }

    if (UWorld* World = GetWorld())
    {
        FTimerManager& TimerManager = World->GetTimerManager();
        TimerManager.ClearTimer(InitiativeAIRollTimer);

        if (InitiativeAIRollDelay > 0.f)
        {
            TimerManager.SetTimer(InitiativeAIRollTimer, this, &UGridBattleManager::PerformAIRoll, InitiativeAIRollDelay, false);
            return;
        }
    }

    PerformAIRoll();
}

void UGridBattleManager::PerformAIRoll()
{
    if (bBattleConcluded || !bAwaitingInitiativeRoll)
    {
        return;
    }

    bool bRolledValue = false;

    if (!bAttackerInitiativeRollRequested && IsSideAIControlled(true))
    {
        bAttackerInitiativeRollRequested = true;
        bRolledValue = true;
    }

    if (!bDefenderInitiativeRollRequested && IsSideAIControlled(false))
    {
        bDefenderInitiativeRollRequested = true;
        bRolledValue = true;
    }

    if (bAttackerInitiativeRollRequested && bDefenderInitiativeRollRequested)
    {
        ResolveInitiativeRollInternal();
        return;
    }

    if (!bRolledValue)
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(InitiativeAIRollTimer);
        }
    }
}

void UGridBattleManager::ScheduleRoundStart(bool bDelayForPresentation)
{
    if (UWorld* World = GetWorld())
    {
        FTimerManager& TimerManager = World->GetTimerManager();
        TimerManager.ClearTimer(InitiativePresentationTimer);

        if (bDelayForPresentation && InitiativePresentationDelay > 0.f)
        {
            TimerManager.SetTimer(InitiativePresentationTimer, this, &UGridBattleManager::FinalizeRoundStart,
                InitiativePresentationDelay, false);
            return;
        }
    }

    FinalizeRoundStart();
}

void UGridBattleManager::FinalizeRoundStart()
{
    if (bBattleConcluded)
    {
        return;
    }

    for (AFighterPawn* Fighter : InitiativeOrder)
    {
        if (Fighter && Fighter->IsAlive())
        {
            Fighter->ResetActivationState();
        }
    }

    OnRoundStarted.Broadcast(CurrentRound, InitiativeWinnerFaction);
    OnActiveFighterChanged.Broadcast(nullptr);
}

bool UGridBattleManager::ShouldPauseForInitiativePrompt() const
{
    const bool bAttackersHuman = !IsSideAIControlled(true);
    const bool bDefendersHuman = !IsSideAIControlled(false);
    return bAttackersHuman || bDefendersHuman;
}

void UGridBattleManager::AdvanceTurn()
{
    UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] AdvanceTurn called. Active fighter: %s"), *DescribeFighter(ActiveFighter));

    if (IsAwaitingAttackPresentation())
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] AdvanceTurn deferred while attack presentation is active."));
        return;
    }

    if (ActiveFighter)
    {
        FinishActivation(ActiveFighter, EGridActivationFinishReason::Auto);
        return;
    }

    const bool bPreviousWasAttacker = bIsAttackerTurn;
    EvaluateRoundProgress(bPreviousWasAttacker);
    OnActiveFighterChanged.Broadcast(nullptr);
}

void UGridBattleManager::ReportAttackResolution(AFighterPawn* Attacker, AFighterPawn* Defender, const FDiceRollResult& Result)
{
    ++PendingAttackPresentationCount;

    const int32 DiceRolled = Result.DiceOutcomes.Num();
    UE_LOG(LogSkaldBattle, Log,
        TEXT("[Battle] Attack resolved: %s -> %s | Dice=%d Hits=%d Crits=%d Misses=%d Damage=%d RemainingHP=%d"),
        *DescribeFighter(Attacker), *DescribeFighter(Defender), DiceRolled, Result.HitCount, Result.CriticalHitCount,
        Result.MissCount, Result.TotalDamage, Result.EndingHealth);

    if (Result.bHighStakesCritical)
    {
        if (UWorld* World = GetWorld())
        {
            if (ASkaldGameState* GameState = World->GetGameState<ASkaldGameState>())
            {
                GameState->RequestTransientSlowdown(0.2f, 0.25f);
            }
        }
    }

    const bool bHadListeners = OnAttackResolved.IsBound();
    OnAttackResolved.Broadcast(Attacker, Defender, Result);

    // NOTE: do NOT define any other functions inside here.
}

// ============================================================
// Manual Dice Roll: apply a player-submitted attack roll to the battle
// ============================================================
void UGridBattleManager::ApplyManualRollFromPlayer(ASkaldPlayerController* Player, AFighterPawn* Attacker, int32 RollValue)
{
    if (!Player || !Attacker)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const int32 ClampedValue = FMath::Clamp(RollValue, 1, 6);

    UE_LOG(LogSkaldBattle, Log, TEXT("[ManualRoll] %s submitted roll %d for %s"),
        *GetNameSafe(Player),
        ClampedValue,
        *GetNameSafe(Attacker));

    // TODO: integrate this value into your actual dice pipeline.
    // For now, this just logs. You can later:
    //  - feed it into your dice manager, or
    //  - store it on the battle manager and have your attack logic read it.
}

void UGridBattleManager::ReportSimulatedAttackResolution(const FDiceRollResult& Result)
{
    ReportAttackResolution(nullptr, nullptr, Result);
}

void UGridBattleManager::ReportAttackRejected(AFighterPawn* Attacker, AFighterPawn* Defender, const FText& Reason)
{
    if (!Attacker)
    {
        return;
    }

    const FString ReasonString = Reason.ToString();
    UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] Attack rejected: %s -> %s | %s"),
        *DescribeFighter(Attacker), *DescribeFighter(Defender), ReasonString.IsEmpty() ? TEXT("<No Reason>") : *ReasonString);

    OnAttackRejected.Broadcast(Attacker, Defender, Reason);
}

bool UGridBattleManager::RegisterPendingFighterDeath(AFighterPawn* Fighter)
{
    if (!Fighter)
    {
        return false;
    }

    PendingFighterDeaths.AddUnique(Fighter);

    if (!IsAwaitingAttackPresentation())
    {
        ProcessPendingFighterDeaths();
    }

    return true;
}

bool UGridBattleManager::CanActivateFighter(AFighterPawn* Fighter) const
{
    if (bBattleConcluded || !Fighter)
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] CanActivateFighter rejected (BattleConcluded=%s, Fighter=%s)"),
            bBattleConcluded ? TEXT("true") : TEXT("false"), *DescribeFighter(Fighter));
        return false;
    }

    if (!Fighter->IsAlive() || !InitiativeOrder.Contains(Fighter))
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] CanActivateFighter rejected (Alive=%s, InOrder=%s) -> %s"),
            Fighter->IsAlive() ? TEXT("true") : TEXT("false"),
            InitiativeOrder.Contains(Fighter) ? TEXT("true") : TEXT("false"), *DescribeFighter(Fighter));
        return false;
    }

    if (ActiveFighter)
    {
        if (ActiveFighter == Fighter)
        {
            UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] CanActivateFighter rejected (Fighter already active) -> %s"),
                *DescribeFighter(Fighter));
            return false;
        }

        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] CanActivateFighter rejected (Another active fighter %s)"),
            *DescribeFighter(ActiveFighter));
        return false;
    }

    if (bAwaitingInitiativeRoll)
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] CanActivateFighter rejected (Awaiting initiative roll) -> %s"),
            *DescribeFighter(Fighter));
        return false;
    }

    if (IsAwaitingAttackPresentation())
    {
        UE_LOG(LogSkaldBattle, Verbose,
            TEXT("[Battle] CanActivateFighter rejected (Awaiting attack presentation) -> %s"),
            *DescribeFighter(Fighter));
        return false;
    }

    if (Fighter->HasActivatedThisRound())
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] CanActivateFighter rejected (Already activated this round) -> %s"),
            *DescribeFighter(Fighter));
        return false;
    }

    if (Fighter->bIsAttacker != bIsAttackerTurn)
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] CanActivateFighter rejected (Wrong side. bIsAttackerTurn=%s) -> %s"),
            bIsAttackerTurn ? TEXT("true") : TEXT("false"), *DescribeFighter(Fighter));
        return false;
    }

    return true;
}

bool UGridBattleManager::ActivateFighter(AFighterPawn* Fighter)
{
    if (!CanActivateFighter(Fighter))
    {
        UE_LOG(LogSkaldBattle, Warning, TEXT("[Battle] ActivateFighter failed for %s"), *DescribeFighter(Fighter));
        return false;
    }

    ActiveFighter = Fighter;
    CurrentTurn = InitiativeOrder.IndexOfByKey(Fighter);
    AFighterPawn* const ActivatedFighter = ActiveFighter;
    if (ActivatedFighter)
    {
        ActivatedFighter->BeginActivation();
    }
    OnActiveFighterChanged.Broadcast(ActiveFighter);
    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Fighter activated: %s (Round=%d, TurnIndex=%d, AttackerTurn=%s)"),
        *DescribeFighter(ActivatedFighter), CurrentRound, CurrentTurn, bIsAttackerTurn ? TEXT("true") : TEXT("false"));
    return ActivatedFighter != nullptr;
}

void UGridBattleManager::FinishActivation(AFighterPawn* Fighter, EGridActivationFinishReason Reason)
{
    if (bBattleConcluded)
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] FinishActivation ignored because battle concluded"));
        return;
    }

    AFighterPawn* FighterToFinish = Fighter ? Fighter : ActiveFighter;
    if (!FighterToFinish)
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] FinishActivation aborted (No fighter)"));
        return;
    }

    if (FighterToFinish->IsResolvingQueuedAttack())
    {
        TWeakObjectPtr<AFighterPawn> FighterPtr = FighterToFinish;
        FDeferredActivationFinish& DeferredRequest = DeferredActivationFinishes.FindOrAdd(FighterPtr);
        DeferredRequest.Reason = Reason;
        DeferredRequest.bWasAttacker = FighterToFinish->bIsAttacker;

        if (!DeferredFinishDelegateHandles.Contains(FighterPtr))
        {
            FDelegateHandle Handle = FighterToFinish->OnQueuedAttackFinalized.AddLambda(
                [this, FighterPtr]()
                {
                    HandleDeferredActivationFinalized(FighterPtr);
                });
            DeferredFinishDelegateHandles.Add(FighterPtr, Handle);
        }

        UE_LOG(LogSkaldBattle, Verbose,
            TEXT("[Battle] Deferring FinishActivation for %s until queued attack completes"),
            *DescribeFighter(FighterToFinish));
        return;
    }

    const bool bWasAttacker = FighterToFinish->bIsAttacker;

    if (Reason == EGridActivationFinishReason::Auto && !IsSideAIControlled(bWasAttacker))
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] Ignoring automatic FinishActivation for human-controlled %s"),
            bWasAttacker ? TEXT("attackers") : TEXT("defenders"));
        return;
    }

    if (IsAwaitingAttackPresentation())
    {
        EnqueueDeferredPresentationFinish(FighterToFinish, Reason);
        UE_LOG(LogSkaldBattle, Verbose,
            TEXT("[Battle] Deferring FinishActivation for %s until attack presentation completes"),
            *DescribeFighter(FighterToFinish));
        return;
    }

    const bool bWasActiveFighter = ActiveFighter == FighterToFinish;

    if (bWasActiveFighter)
    {
        ActiveFighter->FinishActivation();
        ActiveFighter = nullptr;
        UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Fighter finished activation: %s"), *DescribeFighter(FighterToFinish));
    }
    else if (InitiativeOrder.Contains(FighterToFinish))
    {
        FighterToFinish->FinishActivation();
        UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Manually finished activation for non-active fighter: %s"), *DescribeFighter(FighterToFinish));
    }

    EvaluateRoundProgress(bWasAttacker);

    if (bWasActiveFighter)
    {
        OnActiveFighterChanged.Broadcast(nullptr);
    }
}

void UGridBattleManager::HandleDeferredActivationFinalized(TWeakObjectPtr<AFighterPawn> FighterPtr)
{
    FDeferredActivationFinish Request;
    if (!DeferredActivationFinishes.RemoveAndCopyValue(FighterPtr, Request))
    {
        ClearDeferredActivationTracking(FighterPtr);
        return;
    }

    AFighterPawn* Fighter = FighterPtr.Get();
    ClearDeferredActivationTracking(FighterPtr);

    if (!Fighter)
    {
        UE_LOG(LogSkaldBattle, Verbose, TEXT("[Battle] Deferred activation target no longer valid; advancing state"));
        ActiveFighter = nullptr;
        EvaluateRoundProgress(Request.bWasAttacker);
        OnActiveFighterChanged.Broadcast(nullptr);
        return;
    }

    FinishActivation(Fighter, Request.Reason);
}

void UGridBattleManager::EnqueueDeferredPresentationFinish(AFighterPawn* Fighter, EGridActivationFinishReason Reason)
{
    if (!Fighter)
    {
        // Treat null entries as the active fighter at resolution time.
        Fighter = ActiveFighter;
    }

    if (!DeferredPresentationFinishes.IsEmpty())
    {
        for (FDeferredPresentationFinish& Existing : DeferredPresentationFinishes)
        {
            if (Existing.Fighter == Fighter)
            {
                Existing.Reason = Reason;
                return;
            }
        }
    }

    FDeferredPresentationFinish& Entry = DeferredPresentationFinishes.AddDefaulted_GetRef();
    Entry.Fighter = Fighter;
    Entry.Reason = Reason;
}

void UGridBattleManager::ProcessDeferredPresentationFinishes()
{
    if (IsAwaitingAttackPresentation() || DeferredPresentationFinishes.Num() == 0)
    {
        return;
    }

    TArray<FDeferredPresentationFinish> Requests = MoveTemp(DeferredPresentationFinishes);
    DeferredPresentationFinishes.Reset();

    for (const FDeferredPresentationFinish& Request : Requests)
    {
        AFighterPawn* Fighter = Request.Fighter.Get();
        FinishActivation(Fighter, Request.Reason);
    }
}

void UGridBattleManager::ProcessPendingFighterDeaths()
{
    if (PendingFighterDeaths.Num() == 0)
    {
        return;
    }

    TArray<TWeakObjectPtr<AFighterPawn>> FightersToHandle = MoveTemp(PendingFighterDeaths);
    PendingFighterDeaths.Reset();

    for (const TWeakObjectPtr<AFighterPawn>& FighterPtr : FightersToHandle)
    {
        AFighterPawn* Fighter = FighterPtr.Get();
        if (!Fighter)
        {
            continue;
        }

        if (Fighter->IsActorBeingDestroyed())
        {
            continue;
        }

        if (UWorld* World = Fighter->GetWorld())
        {
            const TWeakObjectPtr<AFighterPawn> LocalPtr = Fighter;
            World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([LocalPtr]()
            {
                if (AFighterPawn* FighterPawn = LocalPtr.Get())
                {
                    if (!FighterPawn->IsActorBeingDestroyed())
                    {
                        FighterPawn->Destroy();
                    }
                }
            }));
            continue;
        }

        Fighter->Destroy();
    }
}

void UGridBattleManager::NotifyAttackPresentationComplete()
{
    if (PendingAttackPresentationCount <= 0)
    {
        UE_LOG(LogSkaldBattle, Verbose,
            TEXT("[Battle] Received attack presentation completion with no pending presentations."));
        PendingAttackPresentationCount = 0;
        ProcessDeferredPresentationFinishes();
        ProcessPendingFighterDeaths();
        if (bRoundStartDeferred)
        {
            bRoundStartDeferred = false;
            StartRound();
        }
        return;
    }

    --PendingAttackPresentationCount;

    if (PendingAttackPresentationCount > 0)
    {
        return;
    }

    PendingAttackPresentationCount = 0;

    ProcessDeferredPresentationFinishes();
    ProcessPendingFighterDeaths();

    if (bRoundStartDeferred)
    {
        bRoundStartDeferred = false;
        StartRound();
    }
}

void UGridBattleManager::ClearDeferredActivationTracking(TWeakObjectPtr<AFighterPawn> FighterPtr)
{
    if (AFighterPawn* Fighter = FighterPtr.Get())
    {
        if (FDelegateHandle* Handle = DeferredFinishDelegateHandles.Find(FighterPtr))
        {
            Fighter->OnQueuedAttackFinalized.Remove(*Handle);
        }
    }

    DeferredFinishDelegateHandles.Remove(FighterPtr);
    DeferredActivationFinishes.Remove(FighterPtr);
}

bool UGridBattleManager::HasLivingFighters(bool bForAttackers) const
{
    for (AFighterPawn* Fighter : InitiativeOrder)
    {
        if (Fighter && Fighter->IsAlive() && Fighter->bIsAttacker == bForAttackers)
        {
            return true;
        }
    }
    return false;
}

bool UGridBattleManager::HasAvailableFighters(bool bForAttackers) const
{
    for (AFighterPawn* Fighter : InitiativeOrder)
    {
        if (!Fighter)
        {
            continue;
        }

        if (!Fighter->IsAlive() || Fighter->bIsAttacker != bForAttackers)
        {
            continue;
        }

        if (!Fighter->HasActivatedThisRound())
        {
            return true;
        }
    }
    return false;
}

void UGridBattleManager::EvaluateRoundProgress(bool bPreviousWasAttacker)
{
    if (bBattleConcluded)
    {
        return;
    }

    if (CurrentRound <= 0)
    {
        ClearInactiveFighters();
        return;
    }

    ClearInactiveFighters();

    if (!HasLivingFighters(true) || !HasLivingFighters(false))
    {
        EndBattle();
        return;
    }

    const bool bOpponentsRemain = HasAvailableFighters(!bPreviousWasAttacker);
    const bool bCurrentRemain = HasAvailableFighters(bPreviousWasAttacker);

    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] EvaluateRoundProgress: PreviousWasAttacker=%s, CurrentRemain=%s, OpponentsRemain=%s"),
        bPreviousWasAttacker ? TEXT("true") : TEXT("false"),
        bCurrentRemain ? TEXT("true") : TEXT("false"),
        bOpponentsRemain ? TEXT("true") : TEXT("false"));

    if (bOpponentsRemain)
    {
        bIsAttackerTurn = !bPreviousWasAttacker;
        UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Switching turn to %s"), bIsAttackerTurn ? TEXT("Attackers") : TEXT("Defenders"));
        return;
    }

    if (bCurrentRemain)
    {
        bIsAttackerTurn = bPreviousWasAttacker;
        UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Keeping turn with %s"), bIsAttackerTurn ? TEXT("Attackers") : TEXT("Defenders"));
        return;
    }

    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] No fighters remain with actions. Advancing to next round."));
    StartRound();
}

void UGridBattleManager::ClearInactiveFighters()
{
    const bool bActiveInvalid = ActiveFighter && (!ActiveFighter->IsAlive() || !InitiativeOrder.Contains(ActiveFighter));

    const int32 BeforeCount = InitiativeOrder.Num();
    InitiativeOrder.RemoveAll([](AFighterPawn* Fighter)
    {
        return !Fighter || !Fighter->IsAlive();
    });
    const int32 Removed = BeforeCount - InitiativeOrder.Num();

    if (bActiveInvalid || (ActiveFighter && !InitiativeOrder.Contains(ActiveFighter)))
    {
        ActiveFighter = nullptr;
        OnActiveFighterChanged.Broadcast(nullptr);
    }

    if (Removed > 0)
    {
        UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Cleared %d inactive fighters from initiative order"), Removed);
    }
}

bool UGridBattleManager::IsSideAIControlled(bool bForAttackers) const
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    const USkaldGameInstance* GameInstance = World->GetGameInstance<USkaldGameInstance>();
    int32 TargetPlayerId = INDEX_NONE;
    bool bPendingBattleAIFlag = true;
    FString ParticipantDisplayName;
    ESkaldFaction ParticipantFaction = ESkaldFaction::None;

    if (GameInstance)
    {
        const FS_BattlePayload& Battle = GameInstance->PendingBattle;
        TargetPlayerId = bForAttackers ? Battle.AttackerPlayerID : Battle.DefenderPlayerID;
        bPendingBattleAIFlag = bForAttackers ? Battle.bAttackerIsAI : Battle.bDefenderIsAI;
        ParticipantDisplayName = bForAttackers ? Battle.AttackerDisplayName : Battle.DefenderDisplayName;
        ParticipantFaction = bForAttackers ? Battle.AttackerFaction : Battle.DefenderFaction;

        if (bPendingBattleAIFlag)
        {
            return true;
        }

        if (TargetPlayerId <= 0)
        {
            const int32 OpponentPlayerId = bForAttackers ? Battle.DefenderPlayerID : Battle.AttackerPlayerID;
            const FString& OpponentDisplayName = bForAttackers ? Battle.DefenderDisplayName : Battle.AttackerDisplayName;
            const bool bOpponentIdentified = OpponentPlayerId > 0 || !OpponentDisplayName.IsEmpty();

            if (bOpponentIdentified)
            {
                return true;
            }

            return false;
        }
    }

    if (!GameInstance)
    {
        return true;
    }

    bool bMatchedPlayerState = false;
    if (const AGameStateBase* GameState = World->GetGameState())
    {
        auto MatchesDisplayName = [](const ASkaldPlayerState* PlayerState,
                                     const FString& TargetName) {
            if (!PlayerState || TargetName.IsEmpty())
            {
                return false;
            }

            FString CandidateName = PlayerState->PlayerDisplayName;
            if (CandidateName.IsEmpty())
            {
                CandidateName = PlayerState->GetResolvedPlayerName(TEXT("GridBattleManagerAIResolve"));
            }

            return !CandidateName.IsEmpty() && CandidateName.Equals(TargetName, ESearchCase::IgnoreCase);
        };

        auto ResolveFactionMatch = [&](ESkaldFaction Faction) -> const ASkaldPlayerState*
        {
            if (Faction == ESkaldFaction::None)
            {
                return nullptr;
            }

            const ASkaldPlayerState* UniqueMatch = nullptr;
            const ASkaldPlayerState* UniqueAIMatch = nullptr;
            int32 MatchCount = 0;
            int32 AIMatchCount = 0;

            for (APlayerState* PlayerState : GameState->PlayerArray)
            {
                if (const ASkaldPlayerState* SkaldPlayerState = Cast<ASkaldPlayerState>(PlayerState))
                {
                    if (SkaldPlayerState->Faction != Faction)
                    {
                        continue;
                    }

                    ++MatchCount;
                    if (!UniqueMatch)
                    {
                        UniqueMatch = SkaldPlayerState;
                    }

                    if (SkaldPlayerState->bIsAI)
                    {
                        ++AIMatchCount;
                        if (!UniqueAIMatch)
                        {
                            UniqueAIMatch = SkaldPlayerState;
                        }
                    }
                }
            }

            if (MatchCount == 1)
            {
                return UniqueMatch;
            }

            if (MatchCount > 1 && UniqueAIMatch && AIMatchCount == 1)
            {
                return UniqueAIMatch;
            }

            return nullptr;
        };

        for (APlayerState* PlayerState : GameState->PlayerArray)
        {
            if (const ASkaldPlayerState* SkaldPlayerState = Cast<ASkaldPlayerState>(PlayerState))
            {
                if (TargetPlayerId > 0 && SkaldPlayerState->GetPlayerId() == TargetPlayerId)
                {
                    bMatchedPlayerState = true;
                    return SkaldPlayerState->bIsAI;
                }
            }
        }

        if (!bMatchedPlayerState && !ParticipantDisplayName.IsEmpty())
        {
            for (APlayerState* PlayerState : GameState->PlayerArray)
            {
                if (const ASkaldPlayerState* SkaldPlayerState = Cast<ASkaldPlayerState>(PlayerState))
                {
                    if (MatchesDisplayName(SkaldPlayerState, ParticipantDisplayName))
                    {
                        bMatchedPlayerState = true;
                        return SkaldPlayerState->bIsAI;
                    }
                }
            }
        }

        if (!bMatchedPlayerState)
        {
            if (const ASkaldPlayerState* FactionMatch = ResolveFactionMatch(ParticipantFaction))
            {
                bMatchedPlayerState = true;
                return FactionMatch->bIsAI;
            }
        }
    }

    if (!bMatchedPlayerState)
    {
        UE_LOG(LogSkaldBattle, Warning, TEXT("[Battle] Unable to resolve controller for %s; assuming %s control."),
            bForAttackers ? TEXT("attackers") : TEXT("defenders"),
            bPendingBattleAIFlag ? TEXT("AI") : TEXT("player"));
        return bPendingBattleAIFlag;
    }

    return bPendingBattleAIFlag;
}

void UGridBattleManager::EndBattle()
{
    if (bBattleConcluded)
    {
        return;
    }
    bBattleConcluded = true;

    AttackerSurvivorUnitCount = 0;
    DefenderSurvivorUnitCount = 0;
    AttackerSurvivorArmyCost = 0;
    DefenderSurvivorArmyCost = 0;

    for (AFighterPawn* Fighter : InitiativeOrder)
    {
        if (Fighter && Fighter->IsAlive())
        {
            if (Fighter->bIsAttacker)
            {
                ++AttackerSurvivorUnitCount;
                AttackerSurvivorArmyCost += Fighter->Stats.ArmyCost;
            }
            else
            {
                ++DefenderSurvivorUnitCount;
                DefenderSurvivorArmyCost += Fighter->Stats.ArmyCost;
            }
        }
    }

    ESkaldFaction Winner = ESkaldFaction::None;
    if (AttackerSurvivorUnitCount > 0 && DefenderSurvivorUnitCount <= 0)
    {
        Winner = AttackerTeam.Num() > 0 ? AttackerTeam[0].Faction : ESkaldFaction::None;
    }
    else if (DefenderSurvivorUnitCount > 0 && AttackerSurvivorUnitCount <= 0)
    {
        Winner = DefenderTeam.Num() > 0 ? DefenderTeam[0].Faction : ESkaldFaction::None;
    }

    const int32 AttackerCasualties = AttackerInitialArmyCost - AttackerSurvivorArmyCost;
    const int32 DefenderCasualties = DefenderInitialArmyCost - DefenderSurvivorArmyCost;

    BattleConclusionWinner = Winner;
    BattleConclusionAttackerCasualties = AttackerCasualties;
    BattleConclusionDefenderCasualties = DefenderCasualties;

    ActiveFighter = nullptr;
    OnActiveFighterChanged.Broadcast(nullptr);

    UWorld* World = GetWorld();
    if (World && BattleConclusionBroadcastDelaySeconds > 0.f)
    {
        World->GetTimerManager().SetTimer(BattleConclusionTimerHandle, this, &UGridBattleManager::BroadcastBattleConcluded,
            BattleConclusionBroadcastDelaySeconds, false);
    }
    else
    {
        BroadcastBattleConcluded();
    }
}

void UGridBattleManager::BroadcastBattleConcluded()
{
    if (!bBattleConcluded)
    {
        return;
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(BattleConclusionTimerHandle);
    }

    UE_LOG(LogSkaldBattle, Log, TEXT("[Battle] Battle ended. Winner=%s, AttackerCasualties=%d, DefenderCasualties=%d"),
        *UEnum::GetValueAsString(BattleConclusionWinner), BattleConclusionAttackerCasualties, BattleConclusionDefenderCasualties);
    OnBattleEnded.Broadcast(BattleConclusionWinner, BattleConclusionAttackerCasualties, BattleConclusionDefenderCasualties);
} // ends BroadcastBattleConcluded()

// ============================================================
// Manual Dice Roll UI Integration (Player HUD control)
// ============================================================

void UGridBattleManager::ShowAttackRollButtonForPlayer(AFighterPawn* Attacker)
{
    if (!Attacker)
    {
        UE_LOG(LogTemp, Warning, TEXT("GridBattleManager::ShowAttackRollButtonForPlayer called with null Attacker"));
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("GridBattleManager::ShowAttackRollButtonForPlayer: No valid world"));
        return;
    }

    if (World->GetNetMode() == NM_Client)
    {
        UE_LOG(LogTemp, Warning, TEXT("GridBattleManager::ShowAttackRollButtonForPlayer called on client; ignoring."));
        return;
    }

    if (Attacker->IsAIControlledParticipant())
    {
        UE_LOG(LogTemp, Verbose, TEXT("GridBattleManager::ShowAttackRollButtonForPlayer: %s is AI-controlled; bypassing roll button."),
            *GetNameSafe(Attacker));

        Attacker->RequestAIAutoManualAttackRoll();
        return;
    }

    TArray<ASkaldPlayerController*> TargetControllers;
    GatherOwningPlayerControllers(World, Attacker, TargetControllers);

    if (TargetControllers.Num() == 0)
    {
        UE_LOG(LogTemp, Log,
            TEXT("GridBattleManager::ShowAttackRollButtonForPlayer: No eligible player controller for %s (Faction=%s). Scheduling auto roll."),
            *GetNameSafe(Attacker), *UEnum::GetValueAsString(Attacker->Faction));

        ScheduleAutoManualAttackRoll(Attacker);
        return;
    }

    ClearAutoManualAttackRoll(Attacker);

    for (ASkaldPlayerController* PC : TargetControllers)
    {
        if (!PC)
        {
            continue;
        }

        UE_LOG(LogTemp, Warning, TEXT("[ManualDice] GridBattleManager calling ClientShowAttackRollButton for %s (Controller=%s)"),
            *GetNameSafe(Attacker), *GetNameSafe(PC));

        PC->ClientShowAttackRollButton(Attacker);
    }
}

void UGridBattleManager::HideAttackRollButton()
{
    HideAttackRollButtonForFighter(nullptr);
}

void UGridBattleManager::HideAttackRollButtonForFighter(AFighterPawn* Attacker)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("GridBattleManager::HideAttackRollButton: No valid world"));
        return;
    }

    if (World->GetNetMode() == NM_Client)
    {
        ASkaldPlayerController* LocalController = Cast<ASkaldPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
        if (!LocalController)
        {
            UE_LOG(LogTemp, Warning, TEXT("GridBattleManager::HideAttackRollButton (client): No valid local controller"));
            return;
        }

        if (UBattleHUDWidget* HUD = LocalController->GetBattleHUD())
        {
            UE_LOG(LogTemp, Warning, TEXT("[ManualDice] GridBattleManager::HideAttackRollButton (client) - Hiding roll button"));
            HUD->SetAttackRollButtonVisibility(false);
        }
        return;
    }

    TArray<ASkaldPlayerController*> TargetControllers;
    GatherOwningPlayerControllers(World, Attacker, TargetControllers);

    if (TargetControllers.Num() == 0)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("GridBattleManager::HideAttackRollButton: No player controllers to notify for %s (Faction=%s)"),
            *GetNameSafe(Attacker), Attacker ? *UEnum::GetValueAsString(Attacker->Faction) : TEXT("<None>"));
        return;
    }

    for (ASkaldPlayerController* PC : TargetControllers)
    {
        if (!PC)
        {
            continue;
        }

        UE_LOG(LogTemp, Warning, TEXT("[ManualDice] GridBattleManager::HideAttackRollButton - Hiding roll button for %s"),
            *GetNameSafe(PC));
        PC->ClientHideAttackRollButton();
    }
}

void UGridBattleManager::ScheduleAutoManualAttackRoll(AFighterPawn* Attacker)
{
    if (!Attacker)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    if (!Attacker->IsAwaitingPhysicalAttackRoll())
    {
        return;
    }

    const TWeakObjectPtr<AFighterPawn> AttackerPtr(Attacker);
    FTimerHandle& TimerHandle = PendingAutoManualAttackRolls.FindOrAdd(AttackerPtr);
    World->GetTimerManager().ClearTimer(TimerHandle);

    FTimerDelegate Delegate = FTimerDelegate::CreateUObject(
        this, &UGridBattleManager::HandleAutoManualAttackRoll, AttackerPtr);
    World->GetTimerManager().SetTimer(
        TimerHandle, Delegate, AutoManualAttackRollDelaySeconds, /*bLoop*/ false);
}

void UGridBattleManager::ClearAutoManualAttackRoll(AFighterPawn* Attacker)
{
    if (!Attacker)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const TWeakObjectPtr<AFighterPawn> AttackerPtr(Attacker);
    if (FTimerHandle* TimerHandle = PendingAutoManualAttackRolls.Find(AttackerPtr))
    {
        World->GetTimerManager().ClearTimer(*TimerHandle);
        PendingAutoManualAttackRolls.Remove(AttackerPtr);
    }
}

void UGridBattleManager::HandleAutoManualAttackRoll(TWeakObjectPtr<AFighterPawn> AttackerPtr)
{
    UWorld* World = GetWorld();
    if (World)
    {
        if (FTimerHandle* TimerHandle = PendingAutoManualAttackRolls.Find(AttackerPtr))
        {
            World->GetTimerManager().ClearTimer(*TimerHandle);
            PendingAutoManualAttackRolls.Remove(AttackerPtr);
        }
    }

    AFighterPawn* Attacker = AttackerPtr.Get();
    if (!Attacker || !Attacker->IsAwaitingPhysicalAttackRoll())
    {
        return;
    }

    // Let the fighter coordinate its AI manual roll timing. When the arena is
    // not yet ready this call simply marks the roll as pending and waits for
    // AFighterPawn::NotifyAIAttackPresentationReady to fire once the camera/dice
    // presentation has finished spawning. This mirrors the player flow and
    // prevents the dice manager from falling back to procedural results before
    // the physical arena is available (which previously produced duplicate
    // ApplyPhysicalRollResults logs for a single attack).
    Attacker->RequestAIAutoManualAttackRoll();
}
