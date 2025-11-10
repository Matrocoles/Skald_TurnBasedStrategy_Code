#include "SkaldDiceManager.h"

#include "DiceRollArena.h"
#include "SkaldDiceD6.h"
#include "SkaldDiceModule.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

void USkaldDiceManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ActiveRolls.Empty();
}

void USkaldDiceManager::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        for (TPair<FGuid, FActiveRoll>& Entry : ActiveRolls)
        {
            if (Entry.Value.CompletionTimerHandle.IsValid())
            {
                World->GetTimerManager().ClearTimer(Entry.Value.CompletionTimerHandle);
            }
            if (Entry.Value.UpdateTimerHandle.IsValid())
            {
                World->GetTimerManager().ClearTimer(Entry.Value.UpdateTimerHandle);
            }
            CleanupRollActors(Entry.Value);
        }
    }
    ActiveRolls.Empty();
    FinalizeDeferredInitiativeCleanup();
    DeferredInitiativeCleanups.Empty();
    SharedInitiativeArena.Reset();
    bHoldInitiativeDiceUntilRelease = false;
    Super::Deinitialize();
}

FGuid USkaldDiceManager::RollDice_D6(int32 NumPlayerDice, int32 NumEnemyDice, bool bForInitiative)
{
    const int32 TotalDice = FMath::Max(NumPlayerDice, 0) + FMath::Max(NumEnemyDice, 0);
    if (TotalDice <= 0)
    {
        UE_LOG(LogSkaldDice, Warning, TEXT("RollDice_D6 called with no dice to roll."));
        return FGuid();
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogSkaldDice, Error, TEXT("SkaldDiceManager has no world context."));
        return FGuid();
    }

    const float MinDuration = Config ? Config->RollDurationMin : 0.75f;
    const float MaxDuration = Config ? Config->RollDurationMax : 1.5f;
    const float Duration = FMath::Clamp(FMath::FRandRange(MinDuration, MaxDuration), MinDuration, MaxDuration);

    const FGuid RollId = FGuid::NewGuid();
    FActiveRoll& Roll = AddRoll(NumPlayerDice, NumEnemyDice, Duration, RollId, bForInitiative);
    Roll.RollId = RollId;
    Roll.StartTime = World->GetTimeSeconds();
    Roll.Duration = Duration;
    Roll.bUseScriptedResults = false;
    Roll.ScriptedResults.Reset();

    const bool bSpawnedPhysical = SpawnPhysicalRoll(Roll);

    OnDiceRollStarted.Broadcast(RollId);

    World->GetTimerManager().SetTimer(Roll.CompletionTimerHandle, FTimerDelegate::CreateUObject(this, &USkaldDiceManager::CompleteRoll, RollId), Duration, false);
    World->GetTimerManager().SetTimer(Roll.UpdateTimerHandle, FTimerDelegate::CreateUObject(this, &USkaldDiceManager::BroadcastInterim, RollId), 0.1f, true);

    if (!bSpawnedPhysical)
    {
        UE_LOG(LogSkaldDice, Verbose, TEXT("Falling back to procedural dice results for roll %s."), *RollId.ToString());
    }

    return RollId;
}

void USkaldDiceManager::SetConfig(UDiceRollConfig* InConfig)
{
    Config = InConfig;
    bDeterministic = Config && Config->bDeterministicSeed;
    if (bDeterministic)
    {
        DeterministicStream.Initialize(Config->Seed);
    }
}

void USkaldDiceManager::SetHoldInitiativeDice(bool bHold)
{
    if (bHoldInitiativeDiceUntilRelease == bHold)
    {
        return;
    }

    bHoldInitiativeDiceUntilRelease = bHold;

    if (bHold)
    {
        SharedInitiativeArena.Reset();
    }
    else
    {
        ReleaseHeldInitiativeDice();
    }
}

void USkaldDiceManager::ReleaseHeldInitiativeDice()
{
    FinalizeDeferredInitiativeCleanup();
    DeferredInitiativeCleanups.Empty();
    SharedInitiativeArena.Reset();
}

TArray<int32> USkaldDiceManager::RollDiceBlocking_D6(int32 NumDice)
{
    return GenerateResults(FMath::Max(0, NumDice));
}

FGuid USkaldDiceManager::PlayScriptedRoll(const TArray<int32>& PlayerResults, const TArray<int32>& EnemyResults, bool bForInitiative, float DurationOverride)
{
    const int32 PlayerDice = PlayerResults.Num();
    const int32 EnemyDice = EnemyResults.Num();
    const int32 TotalDice = PlayerDice + EnemyDice;
    if (TotalDice <= 0)
    {
        UE_LOG(LogSkaldDice, Warning, TEXT("PlayScriptedRoll called with no results."));
        return FGuid();
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogSkaldDice, Error, TEXT("SkaldDiceManager has no world context."));
        return FGuid();
    }

    const float MinDuration = Config ? Config->RollDurationMin : 0.75f;
    const float MaxDuration = Config ? Config->RollDurationMax : 1.5f;
    const float Duration = DurationOverride > 0.f ? DurationOverride : FMath::Clamp(FMath::FRandRange(MinDuration, MaxDuration), MinDuration, MaxDuration);

    const FGuid RollId = FGuid::NewGuid();
    FActiveRoll& Roll = AddRoll(PlayerDice, EnemyDice, Duration, RollId, bForInitiative);
    Roll.RollId = RollId;
    Roll.StartTime = World->GetTimeSeconds();
    Roll.Duration = Duration;
    Roll.bUseScriptedResults = true;
    Roll.ScriptedResults.Reset(TotalDice);
    Roll.ScriptedResults.Append(PlayerResults);
    Roll.ScriptedResults.Append(EnemyResults);

    const bool bSpawnedPhysical = SpawnPhysicalRoll(Roll, &PlayerResults, &EnemyResults);

    OnDiceRollStarted.Broadcast(RollId);

    World->GetTimerManager().SetTimer(Roll.CompletionTimerHandle, FTimerDelegate::CreateUObject(this, &USkaldDiceManager::CompleteRoll, RollId), Duration, false);
    World->GetTimerManager().SetTimer(Roll.UpdateTimerHandle, FTimerDelegate::CreateUObject(this, &USkaldDiceManager::BroadcastInterim, RollId), 0.1f, true);

    if (!bSpawnedPhysical)
    {
        UE_LOG(LogSkaldDice, Verbose, TEXT("Scripted roll %s is using fallback results."), *RollId.ToString());
    }

    return RollId;
}

float USkaldDiceManager::GetCleanupDelay() const
{
    if (!Config)
    {
        return 0.f;
    }

    return FMath::Max(Config->ArenaCleanupDelay, Config->DiceCleanupDelay);
}

USkaldDiceManager::FActiveRoll& USkaldDiceManager::AddRoll(int32 PlayerDice, int32 EnemyDice, float Duration, const FGuid& RollId, bool bForInitiative)
{
    FActiveRoll& Entry = ActiveRolls.FindOrAdd(RollId);
    Entry.RollId = RollId;
    Entry.PlayerDice = FMath::Max(PlayerDice, 0);
    Entry.EnemyDice = FMath::Max(EnemyDice, 0);
    Entry.TotalDice = Entry.PlayerDice + Entry.EnemyDice;
    Entry.Duration = Duration;
    Entry.bIsInitiative = bForInitiative;
    Entry.bUseScriptedResults = false;
    Entry.ScriptedResults.Reset();
    Entry.Dice.Reset();
    Entry.Arena.Reset();
    return Entry;
}

void USkaldDiceManager::CompleteRoll(FGuid RollId)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActiveRoll* Roll = ActiveRolls.Find(RollId);
    if (!Roll)
    {
        return;
    }

    if (Roll->UpdateTimerHandle.IsValid())
    {
        World->GetTimerManager().ClearTimer(Roll->UpdateTimerHandle);
    }

    TArray<int32> FinalResults;
    FinalResults.Reserve(Roll->TotalDice);

    if (Roll->Dice.Num() > 0)
    {
        auto GenerateFallbackFace = [this]()
        {
            return bDeterministic ? DeterministicStream.RandRange(1, 6) : FMath::RandRange(1, 6);
        };

        for (FActiveRoll::FDieState& DieState : Roll->Dice)
        {
            ASkaldDiceD6* DiceActor = DieState.Actor.Get();
            int32 Value = DieState.ResolvedValue;

            if (DiceActor)
            {
                const bool bShouldSnap = DieState.bSnapToDesiredValue && DieState.DesiredValue != INDEX_NONE;
                const bool bValueOutOfRange = Value == INDEX_NONE || Value < 1 || Value > 6;

                if (bShouldSnap && DieState.DesiredValue != Value)
                {
                    DiceActor->ForceFaceValue(DieState.DesiredValue, false);
                    Value = DieState.DesiredValue;
                }

                if (bValueOutOfRange)
                {
                    Value = DiceActor->SampleFaceValue();
                }
            }
            else if (Value == INDEX_NONE && DieState.DesiredValue != INDEX_NONE)
            {
                Value = DieState.DesiredValue;
            }

            if (Value == INDEX_NONE || Value < 1 || Value > 6)
            {
                Value = GenerateFallbackFace();
            }

            FinalResults.Add(FMath::Clamp(Value, 1, 6));
        }
    }
    else if (Roll->bUseScriptedResults && Roll->ScriptedResults.Num() == Roll->TotalDice)
    {
        FinalResults = Roll->ScriptedResults;
    }
    else
    {
        FinalResults = GenerateResults(Roll->TotalDice);
    }

    OnDiceRollCompleted.Broadcast(RollId, FinalResults);

    if (Roll->CompletionTimerHandle.IsValid())
    {
        World->GetTimerManager().ClearTimer(Roll->CompletionTimerHandle);
    }

    if (Roll->bIsInitiative && bHoldInitiativeDiceUntilRelease)
    {
        QueueDeferredInitiativeCleanup(*Roll);
    }
    else
    {
        CleanupRollActors(*Roll);
    }
    ActiveRolls.Remove(RollId);
}

void USkaldDiceManager::BroadcastInterim(FGuid RollId)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActiveRoll* Roll = ActiveRolls.Find(RollId);
    if (!Roll)
    {
        return;
    }

    const float Elapsed = World->GetTimeSeconds() - Roll->StartTime;
    OnDiceInterimUpdate.Broadcast(RollId, Elapsed);

    const float Duration = Roll->Duration;
    if (Config && Duration > 0.f && Elapsed >= Duration)
    {
        CompleteRoll(RollId);
    }
}

bool USkaldDiceManager::SpawnPhysicalRoll(FActiveRoll& Roll, const TArray<int32>* PlayerResults, const TArray<int32>* EnemyResults)
{
    if (!Config)
    {
        UE_LOG(LogSkaldDice, Verbose, TEXT("SpawnPhysicalRoll aborted: no dice roll config available."));
        return false;
    }

    if (Config->bUseSpriteOnly)
    {
        UE_LOG(LogSkaldDice, Verbose, TEXT("SpawnPhysicalRoll aborted: active config '%s' is set to sprite-only."), *GetNameSafe(Config));
        return false;
    }

    if (!Config->ArenaClass || (!Config->PlayerDiceClass && !Config->EnemyDiceClass))
    {
        UE_LOG(LogSkaldDice, Warning, TEXT("SpawnPhysicalRoll aborted: config '%s' is missing an arena class or dice class."), *GetNameSafe(Config));
        return false;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogSkaldDice, Error, TEXT("SpawnPhysicalRoll aborted: subsystem has no valid world."));
        return false;
    }

    FVector ArenaLocation = Config->ArenaBounds.GetCenter();
    FRotator ArenaRotation = FRotator::ZeroRotator;
    ADiceRollArena* Arena = ResolveInitiativeArenaForRoll(Roll);

    if (!Arena)
    {
        if (Config->bAnchorArenaToCamera)
        {
            if (APlayerController* PlayerController = World->GetFirstPlayerController())
            {
                FVector ViewLocation = FVector::ZeroVector;
                FRotator ViewRotation = FRotator::ZeroRotator;
                PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

                const FVector Offset = ViewRotation.RotateVector(Config->ArenaCameraRelativeOffset);
                ArenaLocation = ViewLocation + Offset;

                if (Config->bMatchCameraYaw)
                {
                    ArenaRotation = FRotator(0.f, ViewRotation.Yaw, 0.f);
                }
            }
        }

        const FTransform ArenaTransform(ArenaRotation, ArenaLocation);

        FActorSpawnParameters ArenaParams;
        ArenaParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ArenaParams.ObjectFlags |= RF_Transient;

        Arena = World->SpawnActorDeferred<ADiceRollArena>(Config->ArenaClass, ArenaTransform);
        if (!Arena)
        {
            UE_LOG(LogSkaldDice, Warning, TEXT("SpawnPhysicalRoll aborted: failed to spawn arena of class %s."), *GetNameSafe(*Config->ArenaClass));
            return false;
        }

        Arena->ConfigureArena(Config);
        Arena->FinishSpawning(ArenaTransform);
        Arena->SetActorLocationAndRotation(ArenaLocation, ArenaRotation);
        Roll.Arena = Arena;

        if (Roll.bIsInitiative && bHoldInitiativeDiceUntilRelease)
        {
            SharedInitiativeArena = Arena;
        }
    }
    else if (Config->bAnchorArenaToCamera)
    {
        if (APlayerController* PlayerController = World->GetFirstPlayerController())
        {
            FVector ViewLocation = FVector::ZeroVector;
            FRotator ViewRotation = FRotator::ZeroRotator;
            PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

            const FVector Offset = ViewRotation.RotateVector(Config->ArenaCameraRelativeOffset);
            ArenaLocation = ViewLocation + Offset;

            if (Config->bMatchCameraYaw)
            {
                ArenaRotation = FRotator(0.f, ViewRotation.Yaw, 0.f);
            }

            Arena->SetActorLocationAndRotation(ArenaLocation, ArenaRotation);
        }
    }

    const FVector ArenaCenter = Arena->GetActorLocation();
    const FVector Extent = Config->ArenaBounds.GetExtent();
    const float SpawnSpread = FMath::Max(Config->SpawnSpread, 10.f);
    const float HeightOffset = FMath::Max(Config->SpawnHeightOffset, 30.f);
    const float FloorZ = ArenaCenter.Z - Extent.Z;
    const float CeilingZ = ArenaCenter.Z + Extent.Z;
    const float MinSpawnZ = FloorZ + 10.f;
    float MaxSpawnZ = CeilingZ - 10.f;
    if (MaxSpawnZ <= MinSpawnZ)
    {
        MaxSpawnZ = MinSpawnZ + 1.f;
    }

    Roll.Dice.Reset();

    auto SpawnDiceForSide = [&](bool bPlayerSide, int32 Count, const TArray<int32>* DesiredValues)
    {
        if (Count <= 0)
        {
            return;
        }

        for (int32 Index = 0; Index < Count; ++Index)
        {
            TSubclassOf<ASkaldDiceD6> DiceClass = bPlayerSide ? Config->PlayerDiceClass : Config->EnemyDiceClass;
            if (!DiceClass)
            {
                DiceClass = Config->PlayerDiceClass ? Config->PlayerDiceClass : Config->EnemyDiceClass;
            }

            if (!DiceClass)
            {
                continue;
            }

            FVector SpawnPosition = ArenaCenter;
            SpawnPosition.X += FMath::FRandRange(-SpawnSpread, SpawnSpread);
            SpawnPosition.Y += FMath::FRandRange(-SpawnSpread, SpawnSpread);

            const float DesiredHeight = FloorZ + HeightOffset;
            const float RandomVariance = FMath::FRandRange(-HeightOffset * 0.25f, HeightOffset * 0.25f);
            float SpawnZ = DesiredHeight + RandomVariance;
            SpawnZ = FMath::Clamp(SpawnZ, MinSpawnZ, MaxSpawnZ);
            SpawnPosition.Z = SpawnZ;

            const FRotator SpawnRotation(0.f, FMath::FRandRange(0.f, 360.f), 0.f);

            FActorSpawnParameters DiceParams;
            DiceParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            DiceParams.ObjectFlags |= RF_Transient;

            ASkaldDiceD6* Dice = World->SpawnActorDeferred<ASkaldDiceD6>(DiceClass, FTransform(SpawnRotation, SpawnPosition));
            if (!Dice)
            {
                UE_LOG(LogSkaldDice, Warning, TEXT("SpawnPhysicalRoll: failed to spawn dice of class %s."), *GetNameSafe(*DiceClass));
                continue;
            }

            Dice->InitialiseFromConfig(Config);
            Dice->SetOwningRollId(Roll.RollId);

            const int32 RequestedValue = (DesiredValues && DesiredValues->IsValidIndex(Index)) ? (*DesiredValues)[Index] : INDEX_NONE;
            const bool bAllowDesiredValue = !Roll.bIsInitiative || Roll.bUseScriptedResults;
            const int32 DesiredValue = bAllowDesiredValue ? RequestedValue : INDEX_NONE;
            Dice->SetDesiredFaceValue(DesiredValue);
            const bool bSnapToDesired = Roll.bIsInitiative;
            Dice->SetShouldSnapToDesired(bSnapToDesired);
            Dice->ApplyTint(bPlayerSide ? Config->PlayerTint : Config->EnemyTint, Config->DiceTintParameter);
            Dice->OnDiceSettled.AddDynamic(this, &USkaldDiceManager::HandleDieSettled);
            Dice->FinishSpawning(FTransform(SpawnRotation, SpawnPosition));

            const float LinearMagnitude = FMath::RandRange(Config->SpawnImpulseRange.X, Config->SpawnImpulseRange.Y);
            FVector LinearDirection = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(0.35f, 1.f)).GetSafeNormal();
            FVector LinearImpulse = LinearDirection * LinearMagnitude;

            if (Config && Config->SpawnPopImpulse > KINDA_SMALL_NUMBER)
            {
                LinearImpulse += FVector::UpVector * Config->SpawnPopImpulse;
            }

            const float AngularMagnitude = FMath::RandRange(Config->AngularImpulseRange.X, Config->AngularImpulseRange.Y);
            FVector AngularDirection = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)).GetSafeNormal();
            FVector AngularImpulse = AngularDirection * AngularMagnitude;

            Dice->LaunchDie(SpawnPosition, SpawnRotation, LinearImpulse, AngularImpulse);

            FActiveRoll::FDieState& DieState = Roll.Dice.AddDefaulted_GetRef();
            DieState.Actor = Dice;
            DieState.DesiredValue = (DesiredValue != INDEX_NONE) ? FMath::Clamp(DesiredValue, 1, 6) : INDEX_NONE;
            DieState.ResolvedValue = INDEX_NONE;
            DieState.bIsPlayerDie = bPlayerSide;
            DieState.bSnapToDesiredValue = bSnapToDesired;
        }
    };

    SpawnDiceForSide(true, Roll.PlayerDice, PlayerResults);
    SpawnDiceForSide(false, Roll.EnemyDice, EnemyResults);

    const bool bSpawnedAll = Roll.Dice.Num() == Roll.TotalDice;
    if (!bSpawnedAll)
    {
        UE_LOG(LogSkaldDice, Warning, TEXT("SpawnPhysicalRoll aborted: spawned %d of %d dice, cleaning up."), Roll.Dice.Num(), Roll.TotalDice);
        for (FActiveRoll::FDieState& DieState : Roll.Dice)
        {
            if (ASkaldDiceD6* Dice = DieState.Actor.Get())
            {
                Dice->OnDiceSettled.RemoveDynamic(this, &USkaldDiceManager::HandleDieSettled);
                Dice->Destroy();
            }
        }

        if (ADiceRollArena* ArenaInstance = Roll.Arena.Get())
        {
            ArenaInstance->Destroy();
        }

        Roll.Dice.Reset();
        Roll.Arena.Reset();
    }

    return bSpawnedAll;
}

void USkaldDiceManager::CleanupRollActors(FActiveRoll& Roll)
{
    const float DiceDelay = Config ? FMath::Max(Config->DiceCleanupDelay, 0.f) : 0.5f;
    const float ArenaDelay = Config ? FMath::Max(Config->ArenaCleanupDelay, DiceDelay) : DiceDelay;

    for (FActiveRoll::FDieState& DieState : Roll.Dice)
    {
        if (ASkaldDiceD6* Dice = DieState.Actor.Get())
        {
            Dice->OnDiceSettled.RemoveDynamic(this, &USkaldDiceManager::HandleDieSettled);
            Dice->SetLifeSpan(DiceDelay);
        }
    }

    if (ADiceRollArena* Arena = Roll.Arena.Get())
    {
        Arena->SetLifeSpan(ArenaDelay);
    }

    Roll.Dice.Reset();
    Roll.Arena.Reset();
}

void USkaldDiceManager::QueueDeferredInitiativeCleanup(FActiveRoll& Roll)
{
    FDeferredInitiativeCleanup& Cleanup = DeferredInitiativeCleanups.FindOrAdd(Roll.RollId);
    Cleanup.Dice.Reset();
    Cleanup.Arena.Reset();
    Cleanup.DiceLifeSpan = Config ? FMath::Max(Config->DiceCleanupDelay, 0.f) : 0.5f;
    Cleanup.ArenaLifeSpan = Config ? FMath::Max(Config->ArenaCleanupDelay, Cleanup.DiceLifeSpan) : Cleanup.DiceLifeSpan;

    for (FActiveRoll::FDieState& DieState : Roll.Dice)
    {
        if (ASkaldDiceD6* Dice = DieState.Actor.Get())
        {
            Dice->OnDiceSettled.RemoveDynamic(this, &USkaldDiceManager::HandleDieSettled);
            Cleanup.Dice.Add(Dice);
        }
    }

    if (ADiceRollArena* Arena = Roll.Arena.Get())
    {
        Cleanup.Arena = Arena;
    }

    Roll.Dice.Reset();
    Roll.Arena.Reset();
}

void USkaldDiceManager::FinalizeDeferredInitiativeCleanup()
{
    for (TPair<FGuid, FDeferredInitiativeCleanup>& Entry : DeferredInitiativeCleanups)
    {
        FDeferredInitiativeCleanup& Cleanup = Entry.Value;

        for (TWeakObjectPtr<ASkaldDiceD6>& DiePtr : Cleanup.Dice)
        {
            if (ASkaldDiceD6* Dice = DiePtr.Get())
            {
                Dice->SetLifeSpan(Cleanup.DiceLifeSpan);
            }
        }

        if (ADiceRollArena* Arena = Cleanup.Arena.Get())
        {
            Arena->SetLifeSpan(Cleanup.ArenaLifeSpan);
        }
    }
}

bool USkaldDiceManager::ShouldReuseInitiativeArena() const
{
    return bHoldInitiativeDiceUntilRelease && SharedInitiativeArena.IsValid();
}

ADiceRollArena* USkaldDiceManager::ResolveInitiativeArenaForRoll(FActiveRoll& Roll)
{
    if (!Roll.bIsInitiative || !bHoldInitiativeDiceUntilRelease)
    {
        return nullptr;
    }

    if (SharedInitiativeArena.IsValid())
    {
        if (ADiceRollArena* Arena = SharedInitiativeArena.Get())
        {
            Roll.Arena = Arena;
            return Arena;
        }

        SharedInitiativeArena.Reset();
    }

    return nullptr;
}

void USkaldDiceManager::HandleDieSettled(ASkaldDiceD6* Dice, int32 FaceValue)
{
    if (!Dice)
    {
        return;
    }

    const FGuid RollId = Dice->GetOwningRollId();
    if (!RollId.IsValid())
    {
        return;
    }

    FActiveRoll* Roll = ActiveRolls.Find(RollId);
    if (!Roll)
    {
        return;
    }

    for (FActiveRoll::FDieState& DieState : Roll->Dice)
    {
        if (DieState.Actor.Get() == Dice)
        {
            int32 FinalValue = FaceValue;
            if (FinalValue < 1 || FinalValue > 6)
            {
                FinalValue = Dice->SampleFaceValue();
            }

            const bool bShouldSnap = DieState.bSnapToDesiredValue && DieState.DesiredValue != INDEX_NONE;
            if (bShouldSnap && DieState.DesiredValue != FinalValue)
            {
                Dice->ForceFaceValue(DieState.DesiredValue, false);
                FinalValue = DieState.DesiredValue;
            }

            DieState.ResolvedValue = FMath::Clamp(FinalValue, 1, 6);
            break;
        }
    }

    bool bAllResolved = true;
    for (const FActiveRoll::FDieState& DieState : Roll->Dice)
    {
        if (DieState.ResolvedValue == INDEX_NONE)
        {
            bAllResolved = false;
            break;
        }
    }

    if (bAllResolved)
    {
        CompleteRoll(RollId);
    }
}

TArray<int32> USkaldDiceManager::GenerateResults(int32 TotalDice)
{
    TArray<int32> Results;
    Results.Reserve(TotalDice);

    auto GenerateFace = [this]()
    {
        if (bDeterministic)
        {
            return DeterministicStream.RandRange(1, 6);
        }
        return FMath::RandRange(1, 6);
    };

    for (int32 Index = 0; Index < TotalDice; ++Index)
    {
        Results.Add(GenerateFace());
    }

    return Results;
}
