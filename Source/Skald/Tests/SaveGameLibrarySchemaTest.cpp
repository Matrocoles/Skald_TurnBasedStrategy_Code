#include "Misc/AutomationTest.h"

#if defined(WITH_AUTOMATION_TESTS)
#if WITH_AUTOMATION_TESTS

#include "Kismet/GameplayStatics.h"
#include "SkaldSaveGame.h"
#include "SkaldSaveGameLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkaldSaveGameLibrarySchemaTest,
                                 "Skald.SaveGame.SchemaAndUtc",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FSkaldSaveGameLibrarySchemaTest::RunTest(const FString &Parameters) {
  USkaldSaveGame *Save = Cast<USkaldSaveGame>(
      UGameplayStatics::CreateSaveGameObject(USkaldSaveGame::StaticClass()));
  TestNotNull(TEXT("Save game object created"), Save);
  if (!Save) {
    return false;
  }

  const bool bSaved =
      USkaldSaveGameLibrary::SaveSkaldGame(Save, TEXT("Automation_SchemaUtc"), 0);
  TestTrue(TEXT("Save succeeds"), bSaved);
  TestTrue(TEXT("UTC save date populated"), Save->SaveDateUtc != FDateTime());
  TestEqual(TEXT("Schema version written"), Save->SaveSchemaVersion,
            USkaldSaveGame::CurrentSchemaVersion);

  return true;
}

#endif
#endif
