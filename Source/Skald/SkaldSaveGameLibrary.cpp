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

  return UGameplayStatics::SaveGameToSlot(SaveGameObject, SlotName, UserIndex);
}

USkaldSaveGame *USkaldSaveGameLibrary::LoadSkaldGame(const FString &SlotName,
                                                     int32 UserIndex) {
  USkaldSaveGame *LoadedGame = Cast<USkaldSaveGame>(
      UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
  if (LoadedGame) {
    LoadedGame->SaveName = SlotName;
  }
  return LoadedGame;
}
