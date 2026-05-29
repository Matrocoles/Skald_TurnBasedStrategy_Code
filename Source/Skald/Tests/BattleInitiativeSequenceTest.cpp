#include "Misc/AutomationTest.h"

#if defined(WITH_AUTOMATION_TESTS)
#if WITH_AUTOMATION_TESTS
#include "FighterPawn.h"
#include "GridBattleManager.h"
#include "Tests/SkaldAutomationTestHelpers.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBattleInitiativeWaitsBeforeTurnSwitchTest,
    "Skald.Battle.Initiative.WaitsBeforeTurnSwitch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleInitiativeWaitsBeforeTurnSwitchTest::RunTest(const FString& Parameters)
{
    Skald::Tests::FScopedAutomationTestWorld TestWorld;
    UWorld* World = TestWorld.Get();
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    UGridBattleManager* Manager = NewObject<UGridBattleManager>(World);
    TestNotNull(TEXT("Battle manager"), Manager);
    if (!Manager)
    {
        return false;
    }

    TArray<FFighter> Attackers;
    Attackers.AddDefaulted(1);
    Attackers[0].Faction = ESkaldFaction::Human;
    Attackers[0].Stats.Health = 5;

    TArray<FFighter> Defenders;
    Defenders.AddDefaulted(1);
    Defenders[0].Faction = ESkaldFaction::Undead;
    Defenders[0].Stats.Health = 5;

    Manager->InitBattle(Attackers, Defenders);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AFighterPawn* Attacker = World->SpawnActor<AFighterPawn>(
        FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    AFighterPawn* Defender = World->SpawnActor<AFighterPawn>(
        FVector(100.f, 0.f, 0.f), FRotator::ZeroRotator, SpawnParams);
    TestNotNull(TEXT("Attacker spawned"), Attacker);
    TestNotNull(TEXT("Defender spawned"), Defender);
    if (!Attacker || !Defender)
    {
        return false;
    }

    Attacker->Faction = ESkaldFaction::Human;
    Attacker->Stats.Health = 5;
    Defender->Faction = ESkaldFaction::Undead;
    Defender->Stats.Health = 5;

    Manager->RegisterFighter(Attacker, true);
    Manager->RegisterFighter(Defender, false);

    Manager->StartRound();
    TestTrue(TEXT("Round pauses for battle initiative"),
             Manager->IsAwaitingInitiativeRoll());
    TestEqual(TEXT("Initiative winner is unset before roll"),
              Manager->GetInitiativeWinner(), ESkaldFaction::None);

    const bool bInitialAttackerTurn = Manager->IsAttackerTurn();
    Manager->AdvanceTurn();

    TestTrue(TEXT("AdvanceTurn keeps waiting for initiative"),
             Manager->IsAwaitingInitiativeRoll());
    TestEqual(TEXT("AdvanceTurn does not switch sides before initiative"),
              Manager->IsAttackerTurn(), bInitialAttackerTurn);
    TestNull(TEXT("No active fighter before initiative resolves"),
             Manager->GetActiveFighter());

    Manager->ConfirmInitiativeRoll(1, 6);

    TestFalse(TEXT("Confirmed roll clears initiative wait"),
              Manager->IsAwaitingInitiativeRoll());
    TestEqual(TEXT("Defender result controls first battle turn"),
              Manager->IsAttackerTurn(), false);
    TestEqual(TEXT("Defender faction wins initiative"),
              Manager->GetInitiativeWinner(), ESkaldFaction::Undead);

    return true;
}

#endif // WITH_AUTOMATION_TESTS
#endif // defined(WITH_AUTOMATION_TESTS)
