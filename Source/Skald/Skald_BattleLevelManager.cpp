#include "Skald_BattleLevelManager.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Skald_BattleGameMode.h"
#include "Skald_GameInstance.h"
#include "SkaldLogging.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "Misc/EngineVersionComparison.h"
#include "Kismet/GameplayStatics.h"

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

    RegisterStreamingTicker();

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

  RegisterWorldDelegates();
  RegisterStreamingTicker();

  StreamingLevel->SetShouldBeVisible(false);
  StreamingLevel->SetShouldBeLoaded(true);

  if (StreamingLevel->IsLevelLoaded()) {
    HandleLevelLoaded();
  }

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
    UnregisterStreamingTicker();
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

  bLastKnownLoadedState = true;

  if (LevelAddedToWorldHandle.IsValid()) {
    FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedToWorldHandle);
    LevelAddedToWorldHandle.Reset();
  }

  ActiveStreamingLevel->SetShouldBeVisible(true);
  UE_LOG(LogSkald, Log, TEXT("BattleLevelManager: Battle level streamed and visible"));

  if (USkaldGameInstance *GI = OwningInstance.Get()) {
    GI->SetTravelPending(false);
    GI->bIsInBattleMap = true;

    if (!GI->GetActiveBattleGameMode() && ActiveStreamingLevel.IsValid()) {
      UWorld *OwningWorld = ActiveStreamingLevel->GetWorld();
      ULevel *LoadedLevel = ActiveStreamingLevel->GetLoadedLevel();
      if (OwningWorld && LoadedLevel && OwningWorld->GetNetMode() != NM_Client) {
        TSubclassOf<ASkald_BattleGameMode> BattleGameModeClass = nullptr;
        if (AWorldSettings *WorldSettings = LoadedLevel->GetWorldSettings()) {
          UClass *DefaultGameModeClass = nullptr;

#if UE_VERSION_OLDER_THAN(5, 5, 0)
          DefaultGameModeClass = WorldSettings->GetDefaultGameMode();
#else
          if (UClass *ResolvedClass = WorldSettings->DefaultGameMode.Get()) {
            DefaultGameModeClass = ResolvedClass;
          }
#endif

          if (DefaultGameModeClass) {
            if (DefaultGameModeClass->IsChildOf(
                    ASkald_BattleGameMode::StaticClass())) {
              BattleGameModeClass = DefaultGameModeClass;
            }
          }
        }

        if (!BattleGameModeClass) {
          BattleGameModeClass = ASkald_BattleGameMode::StaticClass();
        }

        if (BattleGameModeClass) {
          const FTransform SpawnTransform = FTransform::Identity;
          FActorSpawnParameters SpawnParams;
          SpawnParams.SpawnCollisionHandlingOverride =
              ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
          SpawnParams.OverrideLevel = LoadedLevel;
          SpawnParams.bDeferConstruction = true;

          ASkald_BattleGameMode *BattleGM = OwningWorld->SpawnActor<ASkald_BattleGameMode>(
              BattleGameModeClass, SpawnTransform, SpawnParams);

          if (BattleGM) {
            FString Error;
            const FString MapName =
                RequestedBattleLevel.IsValid()
                    ? RequestedBattleLevel.ToSoftObjectPath().ToString()
                    : LoadedLevel->GetPackage()->GetName();
            BattleGM->InitializeBattleGameMode(MapName, FString(), Error);

            UGameplayStatics::FinishSpawningActor(BattleGM, SpawnTransform);

            ActiveBattleGameMode = BattleGM;
            GI->SetActiveBattleGameMode(BattleGM);

            if (!Error.IsEmpty()) {
              UE_LOG(LogSkald, Warning,
                     TEXT("BattleLevelManager: InitGame for %s reported: %s"),
                     *GetNameSafe(BattleGM), *Error);
            }

            UE_LOG(LogSkald, Log,
                   TEXT("BattleLevelManager: Spawned battle game mode %s (Class=%s)"),
                   *GetNameSafe(BattleGM), *GetNameSafe(BattleGameModeClass));
          } else {
            UE_LOG(LogSkald, Error,
                   TEXT("BattleLevelManager: Failed to spawn battle game mode from %s"),
                   *GetNameSafe(BattleGameModeClass));
          }
        }
      }
    }
  }
}

void USkaldBattleLevelManager::HandleLevelUnloaded() {
  UE_LOG(LogSkald, Log, TEXT("BattleLevelManager: Battle level unloaded"));

  UnregisterWorldDelegates();
  UnregisterStreamingTicker();

  bActiveLevelShouldBeLoaded = false;
  bLastKnownLoadedState = false;
  ActiveStreamingLevel.Reset();
  RequestedBattleLevel.Reset();
  PendingPayload = FS_BattlePayload();

  if (USkaldGameInstance *GI = OwningInstance.Get()) {
    if (ActiveBattleGameMode.IsValid()) {
      if (ASkald_BattleGameMode *BattleGM = ActiveBattleGameMode.Get()) {
        if (!BattleGM->IsActorBeingDestroyed()) {
          BattleGM->Destroy();
        }
      }
      ActiveBattleGameMode.Reset();
    }
    GI->SetActiveBattleGameMode(nullptr);
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

void USkaldBattleLevelManager::RegisterStreamingTicker() {
  UnregisterStreamingTicker();

  if (!ActiveStreamingLevel.IsValid()) {
    bLastKnownLoadedState = false;
    return;
  }

  bLastKnownLoadedState = ActiveStreamingLevel->IsLevelLoaded();
  StreamingStatusTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
      FTickerDelegate::CreateUObject(this, &USkaldBattleLevelManager::TickStreamingStatus),
      0.0f);
}

void USkaldBattleLevelManager::UnregisterStreamingTicker() {
  if (StreamingStatusTickerHandle.IsValid()) {
    FTSTicker::GetCoreTicker().RemoveTicker(StreamingStatusTickerHandle);
    StreamingStatusTickerHandle.Reset();
  }
}

bool USkaldBattleLevelManager::TickStreamingStatus(float DeltaTime) {
  if (!ActiveStreamingLevel.IsValid()) {
    HandleLevelUnloaded();
    bLastKnownLoadedState = false;
    return false;
  }

  ULevelStreamingDynamic *StreamingLevel = ActiveStreamingLevel.Get();
  if (!StreamingLevel) {
    HandleLevelUnloaded();
    bLastKnownLoadedState = false;
    return false;
  }

  const bool bIsLoaded = StreamingLevel->IsLevelLoaded();
  if (bIsLoaded != bLastKnownLoadedState) {
    bLastKnownLoadedState = bIsLoaded;

    if (bIsLoaded) {
      HandleLevelLoaded();
    } else {
      HandleLevelUnloaded();
      return false;
    }
  }

  return ActiveStreamingLevel.IsValid();
}

