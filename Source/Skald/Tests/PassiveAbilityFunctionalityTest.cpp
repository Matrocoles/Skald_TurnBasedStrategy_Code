#include "Tests/PassiveAbilityFunctionalityTest.h"

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Abilities/SkaldAbilityComponent.h"
#include "FighterPawn.h"
#include "GridBattleManager.h"
#include "GridOverlayComponent.h"
#include "Tests/SkaldAutomationTestHelpers.h"
#include "Engine/World.h"
#include "Containers/Set.h"

UCLASS()
class UTestGridOverlayComponent final : public UGridOverlayComponent
{
    GENERATED_BODY()

public:
    void SetDifficultCell(const FIntPoint& Cell, bool bIsDifficult)
    {
        if (bIsDifficult)
        {
            DifficultCells.Add(Cell);
        }
        else
        {
            DifficultCells.Remove(Cell);
        }
    }

    virtual bool IsDifficultTerrain(const FIntPoint& GridCoord) const override
    {
        return DifficultCells.Contains(GridCoord);
    }

private:
    UPROPERTY()
    TSet<FIntPoint> DifficultCells;
};

constexpr EAutomationTestFlags::Type DefaultTestFlags =
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPassiveUndeadNecroticResilienceTest,
    "Skald.Ability.Passive.UndeadNecroticResilience",
    DefaultTestFlags)

bool FPassiveUndeadNecroticResilienceTest::RunTest(const FString& Parameters)
{
    Skald::Tests::FScopedAutomationTestWorld TestWorld;
    UWorld* World = TestWorld.Get();
    TestNotNull(TEXT("World"), World);
    if (!World)
    {
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AFighterPawn* Human = World->SpawnActor<AFighterPawn>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    AFighterPawn* Undead = World->SpawnActor<AFighterPawn>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    TestNotNull(TEXT("Human fighter"), Human);
    TestNotNull(TEXT("Undead fighter"), Undead);
    if (!Human || !Undead)
    {
        return false;
    }

    Human->Faction = ESkaldFaction::Human;
    Human->Stats.Health = 3;
    Human->Stats.Strength = 4;
    Human->MaxHealth = Human->Stats.Health;

    Undead->Faction = ESkaldFaction::Undead;
    Undead->Stats.Health = 3;
    Undead->Stats.Strength = 4;
    Undead->MaxHealth = Undead->Stats.Health;

    World->Tick(LEVELTICK_All, 0.0f);

    if (USkaldAbilityComponent* HumanAbility = Human->GetAbilityComponent())
    {
        HumanAbility->RefreshAbilityLoadout(Human->Stats, Human->Faction);
    }
    if (USkaldAbilityComponent* UndeadAbility = Undead->GetAbilityComponent())
    {
        UndeadAbility->RefreshAbilityLoadout(Undead->Stats, Undead->Faction);
    }

    const int32 HumanBaseStrength = Human->Stats.Strength;
    const int32 UndeadBaseStrength = Undead->Stats.Strength;
    const int32 UndeadBaseDefence = Undead->Stats.Defence;
    const int32 UndeadBaseAttackDice = Undead->Stats.AttackDice;

    Human->Stats.Health = 1;
    Human->OnHealthChanged.Broadcast(1);
    TestEqual(TEXT("Human loses strength at 1 HP"), Human->Stats.Strength, HumanBaseStrength - 1);

    Human->Stats.Health = 3;
    Human->OnHealthChanged.Broadcast(3);
    TestEqual(TEXT("Human regains strength after healing"), Human->Stats.Strength, HumanBaseStrength);

    Undead->Stats.Health = 1;
    Undead->OnHealthChanged.Broadcast(1);
    TestEqual(TEXT("Undead retains strength at 1 HP"), Undead->Stats.Strength, UndeadBaseStrength);
    TestEqual(TEXT("Undead gains defence at 1 HP"), Undead->Stats.Defence, UndeadBaseDefence + 1);
    TestEqual(TEXT("Undead gains attack dice at 1 HP"), Undead->Stats.AttackDice, UndeadBaseAttackDice + 1);

    Undead->Stats.Health = 3;
    Undead->OnHealthChanged.Broadcast(3);
    TestEqual(TEXT("Undead strength unaffected after healing"), Undead->Stats.Strength, UndeadBaseStrength);
    TestEqual(TEXT("Undead defence bonus removed after healing"), Undead->Stats.Defence, UndeadBaseDefence);
    TestEqual(TEXT("Undead attack dice bonus removed after healing"), Undead->Stats.AttackDice, UndeadBaseAttackDice);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFrogPassiveDifficultTerrainTest,
    "Skald.Ability.Passive.FrogTerrainCost",
    DefaultTestFlags)

bool FFrogPassiveDifficultTerrainTest::RunTest(const FString& Parameters)
{
    Skald::Tests::FScopedAutomationTestWorld TestWorld;
    UWorld* World = TestWorld.Get();
    TestNotNull(TEXT("World"), World);
    if (!World)
    {
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AFighterPawn* Human = World->SpawnActor<AFighterPawn>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    AFighterPawn* Frog = World->SpawnActor<AFighterPawn>(FVector(100.f, 0.f, 0.f), FRotator::ZeroRotator, SpawnParams);
    TestNotNull(TEXT("Human fighter"), Human);
    TestNotNull(TEXT("Frog fighter"), Frog);
    if (!Human || !Frog)
    {
        return false;
    }

    Human->Faction = ESkaldFaction::Human;
    Human->Stats.Health = 3;
    Human->Stats.Strength = 4;
    Human->MaxHealth = Human->Stats.Health;

    Frog->Faction = ESkaldFaction::FrogFolk;
    Frog->Stats.Health = 3;
    Frog->Stats.Strength = 4;
    Frog->MaxHealth = Frog->Stats.Health;

    World->Tick(LEVELTICK_All, 0.0f);

    if (USkaldAbilityComponent* HumanAbility = Human->GetAbilityComponent())
    {
        HumanAbility->RefreshAbilityLoadout(Human->Stats, Human->Faction);
    }
    if (USkaldAbilityComponent* FrogAbility = Frog->GetAbilityComponent())
    {
        FrogAbility->RefreshAbilityLoadout(Frog->Stats, Frog->Faction);
    }

    UTestGridOverlayComponent* Grid = NewObject<UTestGridOverlayComponent>();
    TestNotNull(TEXT("Test grid"), Grid);
    if (!Grid)
    {
        return false;
    }

    Grid->SetDifficultCell(FIntPoint::ZeroValue, true);
    Grid->SetDifficultCell(FIntPoint(1, 0), true);

    const int32 HumanCost = Human->GetMovementStepCost(FIntPoint::ZeroValue, FIntPoint(1, 0), Grid);
    const int32 FrogCost = Frog->GetMovementStepCost(FIntPoint::ZeroValue, FIntPoint(1, 0), Grid);

    TestEqual(TEXT("Humans pay difficult terrain cost"), HumanCost, 2);
    TestEqual(TEXT("Frogs ignore difficult terrain"), FrogCost, 1);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FEmpirePassiveInitiativeRerollTest,
    "Skald.Ability.Passive.EmpireInitiativeReroll",
    DefaultTestFlags)

bool FEmpirePassiveInitiativeRerollTest::RunTest(const FString& Parameters)
{
    UGridBattleManager* Manager = NewObject<UGridBattleManager>();
    TestNotNull(TEXT("Battle manager"), Manager);
    if (!Manager)
    {
        return false;
    }

    TArray<FFighter> Attackers;
    Attackers.AddDefaulted(1);
    Attackers[0].Faction = ESkaldFaction::Empire;
    Attackers[0].Stats.Health = 5;

    TArray<FFighter> Defenders;
    Defenders.AddDefaulted(1);
    Defenders[0].Faction = ESkaldFaction::Human;
    Defenders[0].Stats.Health = 5;

    Manager->InitBattle(Attackers, Defenders);

    const int32 Seed = 1337;
    Manager->SetRandomSeed(Seed);
    Manager->RollInitiative();

    const int32 AttackerRoll = Manager->GetLastInitiativeRollAttacker();
    const int32 DefenderRoll = Manager->GetLastInitiativeRollDefender();

    FRandomStream Stream;
    Stream.Initialize(Seed);
    const int32 InitialAttacker = Stream.RandRange(1, UGridBattleManager::InitiativeDiceSides);
    const int32 InitialDefender = Stream.RandRange(1, UGridBattleManager::InitiativeDiceSides);
    const int32 AttackerReroll = Stream.RandRange(1, UGridBattleManager::InitiativeDiceSides);

    TestTrue(TEXT("Empire reroll at least matches initial"), AttackerRoll >= InitialAttacker);
    if (AttackerReroll > InitialAttacker)
    {
        TestEqual(TEXT("Empire uses better reroll"), AttackerRoll, AttackerReroll);
    }
    else
    {
        TestEqual(TEXT("Empire keeps initial roll when better"), AttackerRoll, InitialAttacker);
    }
    TestEqual(TEXT("Defender roll unchanged without passive"), DefenderRoll, InitialDefender);

    UGridBattleManager* BothEmpireManager = NewObject<UGridBattleManager>();
    TestNotNull(TEXT("Second battle manager"), BothEmpireManager);
    if (!BothEmpireManager)
    {
        return false;
    }

    TArray<FFighter> EmpireAttackers = Attackers;
    TArray<FFighter> EmpireDefenders = Attackers;

    BothEmpireManager->InitBattle(EmpireAttackers, EmpireDefenders);

    const int32 SecondSeed = 2024;
    BothEmpireManager->SetRandomSeed(SecondSeed);
    BothEmpireManager->RollInitiative();

    const int32 BothAttackerRoll = BothEmpireManager->GetLastInitiativeRollAttacker();
    const int32 BothDefenderRoll = BothEmpireManager->GetLastInitiativeRollDefender();

    FRandomStream SecondStream;
    SecondStream.Initialize(SecondSeed);
    const int32 InitialAttacker2 = SecondStream.RandRange(1, UGridBattleManager::InitiativeDiceSides);
    const int32 InitialDefender2 = SecondStream.RandRange(1, UGridBattleManager::InitiativeDiceSides);
    const int32 AttackerReroll2 = SecondStream.RandRange(1, UGridBattleManager::InitiativeDiceSides);
    const int32 DefenderReroll2 = SecondStream.RandRange(1, UGridBattleManager::InitiativeDiceSides);

    TestTrue(TEXT("Both empire attacker benefits from reroll"), BothAttackerRoll >= InitialAttacker2);
    if (AttackerReroll2 > InitialAttacker2)
    {
        TestEqual(TEXT("Both empire attacker takes better reroll"), BothAttackerRoll, AttackerReroll2);
    }
    else
    {
        TestEqual(TEXT("Both empire attacker keeps initial"), BothAttackerRoll, InitialAttacker2);
    }

    TestTrue(TEXT("Both empire defender benefits from reroll"), BothDefenderRoll >= InitialDefender2);
    if (DefenderReroll2 > InitialDefender2)
    {
        TestEqual(TEXT("Both empire defender takes better reroll"), BothDefenderRoll, DefenderReroll2);
    }
    else
    {
        TestEqual(TEXT("Both empire defender keeps initial"), BothDefenderRoll, InitialDefender2);
    }

    return true;
}

#endif // WITH_AUTOMATION_TESTS
