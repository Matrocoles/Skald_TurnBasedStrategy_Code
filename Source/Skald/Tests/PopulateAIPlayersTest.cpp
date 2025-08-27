#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Skald_GameMode.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Skald_GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldPopulateAIPlayersTest, "Skald.AI.PopulateCreatesControllers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkaldPopulateAIPlayersTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    USkaldGameInstance* GI = NewObject<USkaldGameInstance>();
    World->SetGameInstance(GI);

    ASkaldGameMode* GM = World->SpawnActor<ASkaldGameMode>();
    TestNotNull(TEXT("GameMode spawned"), GM);
    if (!GM)
    {
        return false;
    }
    GM->BeginPlay();

    ASkaldPlayerController* HumanPC = World->SpawnActor<ASkaldPlayerController>();
    ASkaldPlayerState* HumanPS = World->SpawnActor<ASkaldPlayerState>();
    HumanPC->PlayerState = HumanPS;

    GM->PostLogin(HumanPC);

    ATurnManager* TM = GM->GetTurnManager();
    TestNotNull(TEXT("TurnManager valid"), TM);
    if (!TM)
    {
        return false;
    }

    const TArray<TWeakObjectPtr<ASkaldPlayerController>>& Controllers = TM->GetControllers();
    TestEqual(TEXT("Total controllers"), Controllers.Num(), 4);

    int32 AICount = 0;
    for (const TWeakObjectPtr<ASkaldPlayerController>& Ptr : Controllers)
    {
        ASkaldPlayerController* PC = Ptr.Get();
        if (!PC)
        {
            continue;
        }
        if (PC->bIsAI)
        {
            ++AICount;
            TestNotNull(TEXT("AI pawn"), PC->GetPawn());
        }
    }
    TestEqual(TEXT("AI controller count"), AICount, 3);

    return true;
}

