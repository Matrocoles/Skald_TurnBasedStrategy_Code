#include "Skald_BattleLevelManager.h"

#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Skald_GameInstance.h"
#include "SkaldLogging.h"
#include "UObject/SoftObjectPath.h"

void USkaldBattleLevelManager::Initialise(USkaldGameInstance *InOwner) {

  OwningInstance = InOwner;
}

bool USkaldBattleLevelManager::RequestBattleLevel(
    UWorld *World, const TSoftObjectPtr<UWorld> &BattleLevel,
    const FS_BattlePayload &BattlePayload) {
  if (!World) {
    UE_LOG(LogSkald, Warning,
           TEXT("BattleLevelManager RequestBattleLevel failed: World is null"));
    return false;
  }

  TSoftObjectPtr<UWorld> LevelToStream = BattleLevel;
  if (LevelToStream.IsNull()) {
    LevelToStream = TSoftObjectPtr<UWorld>(
        FSoftObjectPath(TEXT("/Game/Blueprints/Maps/BattleMap.BattleMap")));
  }

  if (!LevelToStream.ToSoftObjectPath().IsValid()) {
    UE_LOG(LogSkald, Warning,
           TEXT("BattleLevelManager RequestBattleLevel failed: Battle level asset invalid"));
    return false;
  }

  if (ActiveStreamingLevel.IsValid()) {
    UE_LOG(LogSkald, Warning,
           TEXT("BattleLevelManager RequestBattleLevel ignored: battle level already active"));
    return false;
  }

  FString MapName = LevelToStream.ToString();
  if (MapName.IsEmpty()) {
    MapName = TEXT("/Game/Blueprints/Maps/BattleMap");
  }

  FVector SpawnLocation = FVector::ZeroVector;
  FRotator SpawnRotation = FRotator::ZeroRotator;
  FString Error;
  ULevelStreamingDynamic *StreamingLevel =
      ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
          World, LevelToStream, SpawnLocation, SpawnRotation, Error);

  if (!StreamingLevel) {
    UE_LOG(LogSkald, Error,
           TEXT("BattleLevelManager RequestBattleLevel failed: %s"), *Error);
    return false;
  }

  RequestedBattleLevel = LevelToStream;
  PendingPayload = BattlePayload;
  ActiveStreamingLevel = StreamingLevel;

  LevelLoadedHandle = StreamingLevel->OnLevelLoaded.AddUObject(
      this, &USkaldBattleLevelManager::HandleLevelLoaded);
  LevelUnloadedHandle = StreamingLevel->OnLevelUnloaded.AddUObject(
      this, &USkaldBattleLevelManager::HandleLevelUnloaded);

  StreamingLevel->SetShouldBeVisible(false);
  StreamingLevel->SetShouldBeLoaded(true);

  if (USkaldGameInstance *GI = OwningInstance.Get()) {
    GI->SetTravelPending(true);
  }

  UE_LOG(LogSkald, Log,
         TEXT("BattleLevelManager: Streaming battle level %s"), *MapName);
  return true;
}

void USkaldBattleLevelManager::ReleaseBattleLevel() {
  if (!ActiveStreamingLevel.IsValid()) {
    return;
  }

  if (USkaldGameInstance *GI = OwningInstance.Get()) {
    GI->SetTravelPending(true);
  }

  if (ULevelStreamingDynamic *StreamingLevel = ActiveStreamingLevel.Get()) {
    StreamingLevel->SetShouldBeVisible(false);
    StreamingLevel->SetShouldBeLoaded(false);
  }

  UE_LOG(LogSkald, Log, TEXT("BattleLevelManager: Unloading battle level"));

}

void USkaldBattleLevelManager::HandleLevelLoaded() {
  if (!ActiveStreamingLevel.IsValid()) {
    return;
  }

  if (LevelLoadedHandle.IsValid()) {
    if (ULevelStreamingDynamic *StreamingLevel = ActiveStreamingLevel.Get()) {
      StreamingLevel->OnLevelLoaded.Remove(LevelLoadedHandle);
    }
    LevelLoadedHandle.Reset();
  }

  ActiveStreamingLevel->SetShouldBeVisible(true);
  UE_LOG(LogSkald, Log, TEXT("BattleLevelManager: Battle level streamed and visible"));

  if (USkaldGameInstance *GI = OwningInstance.Get()) {
    GI->SetTravelPending(false);
    GI->bIsInBattleMap = true;
  }
}

void USkaldBattleLevelManager::HandleLevelUnloaded() {
  UE_LOG(LogSkald, Log, TEXT("BattleLevelManager: Battle level unloaded"));

  if (ULevelStreamingDynamic *StreamingLevel = ActiveStreamingLevel.Get()) {
    if (LevelLoadedHandle.IsValid()) {
      StreamingLevel->OnLevelLoaded.Remove(LevelLoadedHandle);
      LevelLoadedHandle.Reset();
    }
    if (LevelUnloadedHandle.IsValid()) {
      StreamingLevel->OnLevelUnloaded.Remove(LevelUnloadedHandle);
      LevelUnloadedHandle.Reset();
    }
  }

  ActiveStreamingLevel.Reset();
  RequestedBattleLevel.Reset();
  PendingPayload = FS_BattlePayload();

  if (USkaldGameInstance *GI = OwningInstance.Get()) {
    GI->SetTravelPending(false);
    GI->bIsInBattleMap = false;
  }
}
