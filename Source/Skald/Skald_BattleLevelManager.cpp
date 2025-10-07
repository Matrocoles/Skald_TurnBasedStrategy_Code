#include "Skald_BattleLevelManager.h"

#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "Skald_GameInstance.h"
#include "SkaldLogging.h"
#include "UObject/Package.h"
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
  bool bLoadSuccess = false;
  ULevelStreamingDynamic *StreamingLevel =
      ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
          World, LevelToStream, SpawnLocation, SpawnRotation, bLoadSuccess);

  if (!StreamingLevel || !bLoadSuccess) {
    UE_LOG(LogSkald, Error,
           TEXT("BattleLevelManager RequestBattleLevel failed: Could not stream battle level"));
    return false;
  }

  RequestedBattleLevel = LevelToStream;
  PendingPayload = BattlePayload;
  ActiveStreamingLevel = StreamingLevel;

  RegisterWorldDelegates();

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
    UnregisterWorldDelegates();
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

  if (LevelAddedToWorldHandle.IsValid()) {
    FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedToWorldHandle);
    LevelAddedToWorldHandle.Reset();
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

  UnregisterWorldDelegates();

  ActiveStreamingLevel.Reset();
  RequestedBattleLevel.Reset();
  PendingPayload = FS_BattlePayload();

  if (USkaldGameInstance *GI = OwningInstance.Get()) {
    GI->SetTravelPending(false);
    GI->bIsInBattleMap = false;
  }
}

void USkaldBattleLevelManager::HandleLevelAddedToWorld(ULevel *InLevel,
                                                      UWorld *InWorld) {
  if (!DoesEventMatchActiveLevel(InLevel, InWorld)) {
    return;
  }

  HandleLevelLoaded();
}

void USkaldBattleLevelManager::HandleLevelRemovedFromWorld(ULevel *InLevel,
                                                           UWorld *InWorld) {
  if (!DoesEventMatchActiveLevel(InLevel, InWorld)) {
    return;
  }

  HandleLevelUnloaded();
}

bool USkaldBattleLevelManager::DoesEventMatchActiveLevel(ULevel *InLevel,
                                                         UWorld *InWorld) const {
  if (!ActiveStreamingLevel.IsValid() || !InLevel || !InWorld) {
    return false;
  }

  if (UWorld *ActiveWorld = ActiveStreamingLevel->GetWorld()) {
    if (ActiveWorld != InWorld) {
      return false;
    }
  }

  if (ActiveStreamingLevel->GetLoadedLevel() == InLevel) {
    return true;
  }

  const FString RequestedPackage =
      RequestedBattleLevel.ToSoftObjectPath().GetLongPackageName();
  if (!RequestedPackage.IsEmpty()) {
    const FString EventPackage =
        InLevel->GetPackage() ? InLevel->GetPackage()->GetName() : FString();
    if (RequestedPackage == EventPackage) {
      return true;
    }
  }

  return false;
}

void USkaldBattleLevelManager::RegisterWorldDelegates() {
  UnregisterWorldDelegates();

  LevelAddedToWorldHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(
      this, &USkaldBattleLevelManager::HandleLevelAddedToWorld);
  LevelRemovedFromWorldHandle = FWorldDelegates::LevelRemovedFromWorld.AddUObject(
      this, &USkaldBattleLevelManager::HandleLevelRemovedFromWorld);
}

void USkaldBattleLevelManager::UnregisterWorldDelegates() {
  if (LevelAddedToWorldHandle.IsValid()) {
    FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedToWorldHandle);
    LevelAddedToWorldHandle.Reset();
  }

  if (LevelRemovedFromWorldHandle.IsValid()) {
    FWorldDelegates::LevelRemovedFromWorld.Remove(LevelRemovedFromWorldHandle);
    LevelRemovedFromWorldHandle.Reset();
  }
}
