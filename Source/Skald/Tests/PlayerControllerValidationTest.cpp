#include "PlayerControllerValidationTest.h"

#if defined(WITH_AUTOMATION_TESTS)
#if WITH_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

#include "Tests/SkaldAutomationTestHelpers.h"
#include "Skald_TurnManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldPlayerControllerValidationFeedbackTest,
                                 "Skald.PlayerController.ValidationFeedback",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FSkaldPlayerControllerValidationFeedbackTest::RunTest(const FString &)
{
    Skald::Tests::FScopedAutomationTestWorld TestWorld;
    UWorld *World = TestWorld.Get();
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    ATestPlayerController *PC = World->SpawnActor<ATestPlayerController>();
    TestNotNull(TEXT("PlayerController"), PC);
    if (!PC)
    {
        return false;
    }

    UTestHUDWidget *HUD = NewObject<UTestHUDWidget>(PC);
    PC->SetHUD(HUD);

    PC->HandleAttackRequested(1, 2, 1, false);
    TestTrue(TEXT("Attack error shown"), !HUD->LastError.IsEmpty());

    HUD->LastError.Empty();
    PC->HandleMoveRequested(1, 2, 1, /*bTransferSiege=*/false);
    TestTrue(TEXT("Move error shown"), !HUD->LastError.IsEmpty());

    HUD->LastError.Empty();

    ATurnManager *TM = World->SpawnActor<ATurnManager>();
    TestNotNull(TEXT("TurnManager"), TM);
    if (!TM)
    {
        return false;
    }

    PC->SetTurnManager(TM);

    FS_BattlePayload PendingBattle;
    PendingBattle.AttackerPlayerID = 1;
    PendingBattle.DefenderPlayerID = 2;
    PendingBattle.FromTerritoryID = 3;
    PendingBattle.TargetTerritoryID = 4;
    PendingBattle.bAttackerIsAI = false;
    PendingBattle.bDefenderIsAI = false;

    TM->RequestPrepareBattle(PendingBattle);

    PC->HandleAttackRequested(1, 2, 1, false);
    const FString ExpectedAttackError = TEXT("A battle is already being prepared. Please wait for it to begin.");
    TestEqual(TEXT("Pending battle error surfaced"), HUD->LastError, ExpectedAttackError);

    FString ValidationError;
    const bool bValidationResult = PC->TestValidateAttack(1, 2, 1, false, &ValidationError);
    TestFalse(TEXT("ValidateAttack should fail when battle pending"), bValidationResult);
    TestEqual(TEXT("ValidateAttack error matches"), ValidationError, ExpectedAttackError);

    return true;
}
#endif  // WITH_AUTOMATION_TESTS
#endif  // defined(WITH_AUTOMATION_TESTS)
