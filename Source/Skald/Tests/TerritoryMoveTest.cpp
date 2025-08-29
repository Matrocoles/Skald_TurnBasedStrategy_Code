#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Territory.h"
#include "Skald_PlayerState.h"
#include "Components/TextRenderComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldTerritoryMoveValidTest, "Skald.Territory.Move.Valid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldTerritoryMoveInvalidTest, "Skald.Territory.Move.Invalid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkaldTerritoryMoveValidTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    ASkaldPlayerState* Player = World->SpawnActor<ASkaldPlayerState>();
    ATerritory* A = World->SpawnActor<ATerritory>();
    ATerritory* B = World->SpawnActor<ATerritory>();
    TestNotNull(TEXT("Player"), Player);
    TestNotNull(TEXT("Territory A"), A);
    TestNotNull(TEXT("Territory B"), B);
    if (!Player || !A || !B)
    {
        return false;
    }

    A->OwningPlayer = Player;
    B->OwningPlayer = Player;
    A->AdjacentTerritories = {B};
    B->AdjacentTerritories = {A};
    A->ArmyStrength = 10;
    B->ArmyStrength = 0;

    bool bMoved = A->MoveTo(B, 5);
    TestTrue(TEXT("Move succeeds"), bMoved);
    TestEqual(TEXT("Origin army reduced"), A->ArmyStrength, 5);
    TestEqual(TEXT("Target army increased"), B->ArmyStrength, 5);

    UTextRenderComponent* ALabel = A->FindComponentByClass<UTextRenderComponent>();
    UTextRenderComponent* BLabel = B->FindComponentByClass<UTextRenderComponent>();
    TestTrue(TEXT("Source label updated"),
             ALabel && ALabel->Text.ToString().Contains("Army: 5"));
    TestTrue(TEXT("Target label updated"),
             BLabel && BLabel->Text.ToString().Contains("Army: 5"));

    return true;
}

bool FSkaldTerritoryMoveInvalidTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    ASkaldPlayerState* Player1 = World->SpawnActor<ASkaldPlayerState>();
    ASkaldPlayerState* Player2 = World->SpawnActor<ASkaldPlayerState>();
    ATerritory* A = World->SpawnActor<ATerritory>();
    ATerritory* B = World->SpawnActor<ATerritory>();
    TestNotNull(TEXT("Player1"), Player1);
    TestNotNull(TEXT("Player2"), Player2);
    TestNotNull(TEXT("Territory A"), A);
    TestNotNull(TEXT("Territory B"), B);
    if (!Player1 || !Player2 || !A || !B)
    {
        return false;
    }

    // Non-adjacent move should fail
    A->OwningPlayer = Player1;
    B->OwningPlayer = Player1;
    A->AdjacentTerritories = {};
    B->AdjacentTerritories = {};
    A->ArmyStrength = 10;
    B->ArmyStrength = 0;
    UTextRenderComponent* ALabel = A->FindComponentByClass<UTextRenderComponent>();
    UTextRenderComponent* BLabel = B->FindComponentByClass<UTextRenderComponent>();
    const FString ALabelBefore = ALabel ? ALabel->Text.ToString() : FString();
    const FString BLabelBefore = BLabel ? BLabel->Text.ToString() : FString();
    bool bMoved = A->MoveTo(B, 5);
    TestFalse(TEXT("Non-adjacent move fails"), bMoved);
    TestEqual(TEXT("Origin army unchanged"), A->ArmyStrength, 10);
    TestEqual(TEXT("Target army unchanged"), B->ArmyStrength, 0);
    TestEqual(TEXT("Source label unchanged"),
             ALabel ? ALabel->Text.ToString() : FString(), ALabelBefore);
    TestEqual(TEXT("Target label unchanged"),
             BLabel ? BLabel->Text.ToString() : FString(), BLabelBefore);

    // Adjacent but different owner should fail
    A->AdjacentTerritories = {B};
    B->AdjacentTerritories = {A};
    B->OwningPlayer = Player2;
    const FString ALabelBefore2 = ALabel ? ALabel->Text.ToString() : FString();
    const FString BLabelBefore2 = BLabel ? BLabel->Text.ToString() : FString();
    bMoved = A->MoveTo(B, 5);
    TestFalse(TEXT("Move to enemy territory fails"), bMoved);
    TestEqual(TEXT("Origin army unchanged"), A->ArmyStrength, 10);
    TestEqual(TEXT("Target army unchanged"), B->ArmyStrength, 0);
    TestEqual(TEXT("Source label unchanged"),
             ALabel ? ALabel->Text.ToString() : FString(), ALabelBefore2);
    TestEqual(TEXT("Target label unchanged"),
             BLabel ? BLabel->Text.ToString() : FString(), BLabelBefore2);

    return true;
}
