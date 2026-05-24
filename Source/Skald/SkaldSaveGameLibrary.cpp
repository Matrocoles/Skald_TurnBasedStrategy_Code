#include "SkaldSaveGameLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "SkaldSaveGame.h"

bool USkaldSaveGameLibrary::SaveSkaldGame(USkaldSaveGame *SaveGameObject,
                                          const FString &SlotName,
                                          int32 UserIndex) {
  if (!SaveGameObject) {
    SaveGameObject = Cast<USkaldSaveGame>(
        UGameplayStatics::CreateSaveGameObject(USkaldSaveGame::StaticClass()));
    if (!SaveGameObject) {
      return false;
    }
  }

  SaveGameObject->SaveName = SlotName;
  SaveGameObject->SaveDate = FDateTime::Now();
  SaveGameObject->SaveDateUtc = FDateTime::UtcNow();
  SaveGameObject->SaveSchemaVersion = USkaldSaveGame::CurrentSchemaVersion;

  return UGameplayStatics::SaveGameToSlot(SaveGameObject, SlotName, UserIndex);
}

USkaldSaveGame *USkaldSaveGameLibrary::LoadSkaldGame(const FString &SlotName,
                                                     int32 UserIndex) {
  USkaldSaveGame *LoadedGame = Cast<USkaldSaveGame>(
      UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
  if (LoadedGame) {
    LoadedGame->SaveName = SlotName;
    if (LoadedGame->SaveDateUtc == FDateTime()) {
      LoadedGame->SaveDateUtc = LoadedGame->SaveDate;
    }
    if (LoadedGame->SaveSchemaVersion <= 0) {
      LoadedGame->SaveSchemaVersion = 1;
    }
  }
  return LoadedGame;
}
