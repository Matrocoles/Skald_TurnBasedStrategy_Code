#include "Skald_BattleLevelManager.h"

#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
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
    const bool bMatchesRequestedLevel =
        RequestedBattleLevel.ToSoftObjectPath() == LevelToStream.ToSoftObjectPath();

    if (!bMatchesRequestedLevel) {
      UE_LOG(LogSkald, Warning,
             TEXT("BattleLevelManager RequestBattleLevel ignored: battle level already active"));
      return false;
    }

    if (!bActiveLevelShouldBeLoaded) {
      UE_LOG(LogSkald, Warning,
             TEXT("BattleLevelManager RequestBattleLevel retry ignored: battle level currently unloading"));
      return false;
    }

    // A streaming request is already in flight for the desired level, so treat
    // the retry as a success and refresh any pending state without issuing a
    // second load request. This prevents fallback OpenLevel travel from
    // tearing down the overworld when additional controllers retry.
    PendingPayload = BattlePayload;
    if (USkaldGameInstance *GI = OwningInstance.Get()) {
      GI->SetTravelPending(true);
    }

    UE_LOG(LogSkald, Log,
           TEXT("BattleLevelManager: Battle level %s already streaming, reusing active request"),
           *LevelToStream.ToString());
    return true;
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
  bActiveLevelShouldBeLoaded = true;

  StreamingLevelLoadedHandle =
      StreamingLevel->OnLevelLoaded.AddUObject(
          this, &USkaldBattleLevelManager::HandleStreamingLevelLoaded);
  StreamingLevelUnloadedHandle =
      StreamingLevel->OnLevelUnloaded.AddUObject(
          this, &USkaldBattleLevelManager::HandleStreamingLevelUnloaded);

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
    UnregisterStreamingDelegates();
    return;
  }

  if (USkaldGameInstance *GI = OwningInstance.Get()) {
    GI->SetTravelPending(true);
  }

  if (ULevelStreamingDynamic *StreamingLevel = ActiveStreamingLevel.Get()) {
    StreamingLevel->SetShouldBeVisible(false);
    StreamingLevel->SetShouldBeLoaded(false);
  }

  bActiveLevelShouldBeLoaded = false;

  UE_LOG(LogSkald, Log, TEXT("BattleLevelManager: Unloading battle level"));

}

void USkaldBattleLevelManager::HandleLevelLoaded() {
  if (!ActiveStreamingLevel.IsValid()) {
    return;
  }

  if (!bActiveLevelShouldBeLoaded) {
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
  UnregisterStreamingDelegates();

  bActiveLevelShouldBeLoaded = false;
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

void USkaldBattleLevelManager::HandleStreamingLevelLoaded(ULevel *InLevel) {
  UE_LOG(LogSkald, Verbose,
         TEXT("BattleLevelManager: Streaming level reported loaded: %s"),
         InLevel ? *InLevel->GetName() : TEXT("<null>"));
  HandleLevelLoaded();
}

void USkaldBattleLevelManager::HandleStreamingLevelUnloaded(ULevel *InLevel) {
  UE_LOG(LogSkald, Verbose,
         TEXT("BattleLevelManager: Streaming level reported unloaded: %s"),
         InLevel ? *InLevel->GetName() : TEXT("<null>"));
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

void USkaldBattleLevelManager::UnregisterStreamingDelegates() {
  if (!ActiveStreamingLevel.IsValid()) {
    StreamingLevelLoadedHandle.Reset();
    StreamingLevelUnloadedHandle.Reset();
    return;
  }

  if (ULevelStreamingDynamic *StreamingLevel = ActiveStreamingLevel.Get()) {
    if (StreamingLevelLoadedHandle.IsValid()) {
      StreamingLevel->OnLevelLoaded.Remove(StreamingLevelLoadedHandle);
      StreamingLevelLoadedHandle.Reset();
    }

    if (StreamingLevelUnloadedHandle.IsValid()) {
      StreamingLevel->OnLevelUnloaded.Remove(StreamingLevelUnloadedHandle);
      StreamingLevelUnloadedHandle.Reset();
    }
  } else {
    StreamingLevelLoadedHandle.Reset();
    StreamingLevelUnloadedHandle.Reset();
  }
}

