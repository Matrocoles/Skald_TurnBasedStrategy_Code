#include "Skald_BattleLevelManager.h"

#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreaming.h"
#include "Containers/Ticker.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Misc/PackageName.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Skald_BattleGameMode.h"
#include "Skald_GameInstance.h"
#include "SkaldLogging.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

namespace
{
static ULevelStreaming *FindStreamingLevelFor(ULevel *Level)
{
  if (!Level) {
    return nullptr;
  }

  UWorld *OwningWorld = Level->GetWorld();
  if (!OwningWorld) {
    return nullptr;
  }

  for (ULevelStreaming *StreamingLevel : OwningWorld->GetStreamingLevels()) {
    if (StreamingLevel && StreamingLevel->GetLoadedLevel() == Level) {
      return StreamingLevel;
    }
  }

  return nullptr;
}

static void SetPersistentLevelVisibility(UWorld *World, ULevel *PersistentLevel,
                                         bool bShouldBeVisible)
{
  if (!PersistentLevel) {
    return;
  }

  if (ULevelStreaming *StreamingLevel = FindStreamingLevelFor(PersistentLevel)) {
    StreamingLevel->SetShouldBeVisible(bShouldBeVisible);
    if (World) {
      World->UpdateLevelStreaming();
    }
  }
}

static FString ResolveStreamingLevelPackageName(const ULevelStreaming *Level)
{
  if (!Level)
  {
    return FString();
  }

  FString PackageName = Level->GetWorldAssetPackageName();
  if (PackageName.IsEmpty())
  {
    PackageName = Level->GetWorldAsset().ToSoftObjectPath().GetLongPackageName();
  }

  if (!PackageName.IsEmpty())
  {
    FString LongPackageName;
    if (FPackageName::TryConvertFilenameToLongPackageName(PackageName, LongPackageName))
    {
      PackageName = MoveTemp(LongPackageName);
    }
  }

  return PackageName;
}
} // namespace

void USkaldBattleLevelManager::Initialise(USkaldGameInstance *InOwner) {

  OwningInstance = InOwner;
}

void USkaldBattleLevelManager::ConfigurePersistentSublevels(
    const TSoftObjectPtr<UWorld>& InOverviewLevel,
    const TSoftObjectPtr<UWorld>& InDiceBoardLevel) {
  ConfiguredOverviewLevel = InOverviewLevel;
  ConfiguredDiceBoardLevel = InDiceBoardLevel;
  UE_LOG(LogSkald, Log,
         TEXT("BattleLevelManager: configured persistent sublevels (overview=%s dice=%s)"),
         *ConfiguredOverviewLevel.ToString(), *ConfiguredDiceBoardLevel.ToString());
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
    RequestedBattleLevel = LevelToStream;
    PendingPayload = BattlePayload;
    bActiveLevelShouldBeLoaded = true;

    const ULevelStreaming *StreamingLevel = ActiveStreamingLevel.Get();
    const bool bLevelAlreadyLoaded =
        StreamingLevel && StreamingLevel->IsLevelLoaded();

    if (USkaldGameInstance *GI = OwningInstance.Get()) {
      if (bLevelAlreadyLoaded) {
        GI->SetTravelPending(false);
      } else {
        GI->SetTravelPending(true);
      }
    }

    RegisterStreamingTicker();

    if (bLevelAlreadyLoaded) {
      HandleLevelLoaded();
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

  ULevelStreaming *StreamingLevel = nullptr;
  bool bLoadSuccess = false;

  const FSoftObjectPath RequestedPath = LevelToStream.ToSoftObjectPath();
  const FString RequestedPackage = RequestedPath.GetLongPackageName();
  const FString RequestedAssetName = RequestedPath.GetAssetName();
  for (ULevelStreaming *ExistingLevel : World->GetStreamingLevels()) {
    if (!ExistingLevel) {
      continue;
    }

    FString ExistingPackage = ExistingLevel->GetWorldAssetPackageName();
    if (ExistingPackage.IsEmpty()) {
      ExistingPackage = ExistingLevel->GetWorldAsset().ToSoftObjectPath().GetLongPackageName();
    }

    FString ExistingAssetName;
    if (ExistingPackage.IsEmpty()) {
      ExistingAssetName = ExistingLevel->GetWorldAsset().ToSoftObjectPath().GetAssetName();
    } else {
      ExistingAssetName = FPackageName::GetShortName(ExistingPackage);
    }

    const bool bPackageMatches = !RequestedPackage.IsEmpty() &&
                                 ExistingPackage.Equals(RequestedPackage, ESearchCase::IgnoreCase);
    const bool bAssetMatches = !RequestedAssetName.IsEmpty() &&
                               ExistingAssetName.Equals(RequestedAssetName, ESearchCase::IgnoreCase);

    if (bPackageMatches || bAssetMatches) {
      StreamingLevel = ExistingLevel;
      bLoadSuccess = true;
      const FString LevelLabel = !ExistingPackage.IsEmpty() ? ExistingPackage : ExistingAssetName;
      UE_LOG(LogSkald, Log,
             TEXT("BattleLevelManager: Using existing streaming level %s"),
             *LevelLabel);
      break;
    }
  }

  if (!StreamingLevel) {
    FVector SpawnLocation = FVector::ZeroVector;
    FRotator SpawnRotation = FRotator::ZeroRotator;
    StreamingLevel = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
        World, LevelToStream, SpawnLocation, SpawnRotation, bLoadSuccess);
  }

  if (StreamingLevel && !bLoadSuccess) {
    StreamingLevel = nullptr;
  }

  if ((!StreamingLevel || !bLoadSuccess) && !RequestedPackage.IsEmpty()) {
    UE_LOG(LogSkald, Verbose,
           TEXT("BattleLevelManager: Dynamic load failed for %s, attempting manual streaming"),
           *RequestedPackage);

    if (!StreamingLevel) {
      ULevelStreamingDynamic *DynamicStreaming =
          NewObject<ULevelStreamingDynamic>(World, NAME_None, RF_Transient);

      if (DynamicStreaming) {
        DynamicStreaming->SetWorldAssetByPackageName(FName(*RequestedPackage));
        DynamicStreaming->SetShouldBeVisible(false);
        DynamicStreaming->SetShouldBeLoaded(false);
        DynamicStreaming->LevelTransform = FTransform::Identity;

        World->AddStreamingLevel(DynamicStreaming);
        StreamingLevel = DynamicStreaming;
      }
    }

    if (StreamingLevel) {
      bLoadSuccess = true;
    }
  }

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
    RestoreNonBattleLevels();
    return;
  }

  if (USkaldGameInstance *GI = OwningInstance.Get()) {
    GI->SetTravelPending(true);
  }

  if (ULevelStreaming *StreamingLevel = ActiveStreamingLevel.Get()) {
    StreamingLevel->SetShouldBeVisible(false);
    StreamingLevel->SetShouldBeLoaded(false);
  }

  bActiveLevelShouldBeLoaded = false;

  UE_LOG(LogSkald, Log, TEXT("BattleLevelManager: Unloading battle level"));

}

bool USkaldBattleLevelManager::SetDiceBoardActive(UWorld* World, bool bShouldBeActive) {
  if (!World) {
    UE_LOG(LogSkald, Warning, TEXT("BattleLevelManager: SetDiceBoardActive failed (world is null)"));
    return false;
  }

  if (ConfiguredDiceBoardLevel.IsNull()) {
    UE_LOG(LogSkald, Warning, TEXT("BattleLevelManager: SetDiceBoardActive failed (dice board level is not configured)"));
    return false;
  }

  if (bShouldBeActive) {
    ULevelStreaming* DiceStreamingLevel = ActiveDiceStreamingLevel.Get();
    if (!DiceStreamingLevel &&
        !ResolveOrStreamLevel(World, ConfiguredDiceBoardLevel, DiceStreamingLevel, TEXT("DiceBoard"))) {
      return false;
    }

    ActiveDiceStreamingLevel = DiceStreamingLevel;
    DiceStreamingLevel->SetShouldBeLoaded(true);
    DiceStreamingLevel->SetShouldBeVisible(true);
    HideNonBattleLevels();
    UE_LOG(LogSkald, Log, TEXT("BattleLevelManager: Dice board activated as streamed sublevel"));
    return true;
  }

  if (ULevelStreaming* DiceStreamingLevel = ActiveDiceStreamingLevel.Get()) {
    DiceStreamingLevel->SetShouldBeVisible(false);
    RestoreNonBattleLevels();
    UE_LOG(LogSkald, Log, TEXT("BattleLevelManager: Dice board hidden"));
  }
  ActiveDiceStreamingLevel.Reset();
  return true;
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
  HideNonBattleLevels();
  UE_LOG(LogSkald, Log, TEXT("BattleLevelManager: Battle level streamed and visible"));

  if (USkaldGameInstance *GI = OwningInstance.Get()) {
    GI->SetTravelPending(false);
    GI->SetBattleMapActive(true);

    if (!GI->GetActiveBattleGameMode() && ActiveStreamingLevel.IsValid()) {
      UWorld *OwningWorld = ActiveStreamingLevel->GetWorld();
      ULevel *LoadedLevel = ActiveStreamingLevel->GetLoadedLevel();
      if (OwningWorld && LoadedLevel && OwningWorld->GetNetMode() != NM_Client) {
        TSubclassOf<ASkald_BattleGameMode> BattleGameModeClass = nullptr;
        if (AWorldSettings *WorldSettings = LoadedLevel->GetWorldSettings()) {
          UClass *DefaultGameModeClass = nullptr;

          if (UClass *ResolvedClass = WorldSettings->DefaultGameMode.Get()) {
            DefaultGameModeClass = ResolvedClass;
          }

          if (DefaultGameModeClass) {
            const FString DefaultClassPath = DefaultGameModeClass->GetPathName();
            UE_LOG(LogSkald, Log,
                   TEXT("BattleLevelManager: Level %s default GameMode=%s (%s)"),
                   *GetNameSafe(LoadedLevel), *GetNameSafe(DefaultGameModeClass),
                   *DefaultClassPath);

            if (!DefaultClassPath.Contains(TEXT("Skald_BattleGameMode_SC"))) {
              UE_LOG(LogSkald, Warning,
                     TEXT("BattleLevelManager: Expected Skald_BattleGameMode_SC as the default battle GameMode for streamed maps."));
            }

            if (DefaultGameModeClass->IsChildOf(
                    ASkald_BattleGameMode::StaticClass())) {
              BattleGameModeClass = DefaultGameModeClass;
            } else {
              UE_LOG(LogSkald, Warning,
                     TEXT("BattleLevelManager: Default GameMode is not derived from Skald_BattleGameMode."));
            }
          }
        }

        if (!BattleGameModeClass ||
            BattleGameModeClass == ASkald_BattleGameMode::StaticClass()) {
          static const TSoftClassPtr<ASkald_BattleGameMode>
              BlueprintBattleGameModeClass(
                  FSoftObjectPath(TEXT("/Game/C++_BPs/Skald_BatlleGameMode_SC.Skald_BatlleGameMode_SC_C")));

          if (!BlueprintBattleGameModeClass.IsNull()) {
            if (UClass *LoadedClass = BlueprintBattleGameModeClass.LoadSynchronous()) {
              BattleGameModeClass = LoadedClass;
            } else {
              UE_LOG(LogSkald, Warning,
                     TEXT("BattleLevelManager: Failed to load Skald_BatlleGameMode_SC blueprint. Falling back to C++ class."));
            }
          }
        }

        if (!BattleGameModeClass) {
          BattleGameModeClass = ASkald_BattleGameMode::StaticClass();
          UE_LOG(LogSkald, Warning,
                 TEXT("BattleLevelManager: Falling back to native Skald_BattleGameMode for battle map."));
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
    ASkald_BattleGameMode *BattleGM = ActiveBattleGameMode.Get();
    if (BattleGM) {
      BattleGM->NotifyBattleLevelActivated();
    } else {
      BattleGM = GI->GetActiveBattleGameMode();
      if (BattleGM) {
        BattleGM->NotifyBattleLevelActivated();
        ActiveBattleGameMode = BattleGM;
      }
    }
  }
}

void USkaldBattleLevelManager::HandleLevelUnloaded() {
  UE_LOG(LogSkald, Log, TEXT("BattleLevelManager: Battle level unloaded"));

  UnregisterWorldDelegates();
  UnregisterStreamingTicker();
  RestoreNonBattleLevels();

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
    GI->SetBattleMapActive(false);
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

  ULevelStreaming *StreamingLevel = ActiveStreamingLevel.Get();
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

void USkaldBattleLevelManager::HideNonBattleLevels() {
  RestoreNonBattleLevels();

  if (!ActiveStreamingLevel.IsValid()) {
    return;
  }

  ULevelStreaming *StreamingLevel = ActiveStreamingLevel.Get();
  if (!StreamingLevel) {
    return;
  }

  UWorld *StreamingWorld = StreamingLevel->GetWorld();
  if (!StreamingWorld) {
    return;
  }

  CachedStreamingWorld = StreamingWorld;

  ULevel *LoadedBattleLevel = StreamingLevel->GetLoadedLevel();

  HiddenStreamingLevels.Reset();

  for (ULevelStreaming *OtherLevel : StreamingWorld->GetStreamingLevels()) {
    if (!OtherLevel || OtherLevel == StreamingLevel) {
      continue;
    }

    if (IsStreamingLevelPartOfBattleMap(OtherLevel)) {
      continue;
    }

    const bool bWasVisible = OtherLevel->IsLevelVisible();
    if (!bWasVisible) {
      continue;
    }

    FHiddenStreamingLevelState State;
    State.Level = OtherLevel;
    State.bWasVisible = bWasVisible;
    HiddenStreamingLevels.Add(State);

    FString LevelLabel = OtherLevel->GetWorldAssetPackageName();
    if (LevelLabel.IsEmpty()) {
      LevelLabel = OtherLevel->GetWorldAsset().ToSoftObjectPath().ToString();
    }

    OtherLevel->SetShouldBeVisible(false);
    UE_LOG(LogSkald, Verbose,
           TEXT("BattleLevelManager: Hiding streaming level %s while battle map active"),
           *LevelLabel);
  }

  HiddenPersistentLevel.Reset();
  HiddenPersistentActors.Reset();
  bPersistentLevelWasVisible = false;

  if (ULevel *PersistentLevel = StreamingWorld->PersistentLevel) {
    if (PersistentLevel != LoadedBattleLevel && PersistentLevel->bIsVisible) {
      HiddenPersistentLevel = PersistentLevel;
      bPersistentLevelWasVisible = PersistentLevel->bIsVisible;

      for (AActor *Actor : PersistentLevel->Actors) {
        if (!Actor || Actor->GetLevel() != PersistentLevel) {
          continue;
        }

        // Skip world settings so the world retains its authoritative
        // configuration while the overworld is hidden.
        if (Actor->IsA<AWorldSettings>()) {
          continue;
        }

        const bool bWasHiddenInGame = Actor->IsHidden();
        const bool bHadCollision = Actor->GetActorEnableCollision();
        const bool bWasTickEnabled = Actor->IsActorTickEnabled();

        if (!bWasHiddenInGame || bHadCollision || bWasTickEnabled) {
          FHiddenPersistentActorState ActorState;
          ActorState.Actor = Actor;
          ActorState.bWasHiddenInGame = bWasHiddenInGame;
          ActorState.bHadCollision = bHadCollision;
          ActorState.bWasTickEnabled = bWasTickEnabled;
          HiddenPersistentActors.Add(ActorState);

          Actor->SetActorHiddenInGame(true);
          Actor->SetActorEnableCollision(false);
          Actor->SetActorTickEnabled(false);
        }
      }

      SetPersistentLevelVisibility(StreamingWorld, PersistentLevel, false);
      UE_LOG(LogSkald, Verbose,
             TEXT("BattleLevelManager: Hiding persistent level %s"),
             *PersistentLevel->GetOutermost()->GetName());
    }
  }
}

void USkaldBattleLevelManager::RestoreNonBattleLevels() {
  if (!HiddenStreamingLevels.Num() && !HiddenPersistentLevel.IsValid() &&
      !HiddenPersistentActors.Num()) {
    CachedStreamingWorld.Reset();
    bPersistentLevelWasVisible = false;
    return;
  }

  UWorld *StreamingWorld = CachedStreamingWorld.Get();
  if (!StreamingWorld && ActiveStreamingLevel.IsValid()) {
    StreamingWorld = ActiveStreamingLevel->GetWorld();
  }

  for (const FHiddenStreamingLevelState &State : HiddenStreamingLevels) {
    if (ULevelStreaming *Level = State.Level.Get()) {
      Level->SetShouldBeVisible(State.bWasVisible);
      if (State.bWasVisible) {
        FString LevelLabel = Level->GetWorldAssetPackageName();
        if (LevelLabel.IsEmpty()) {
          LevelLabel = Level->GetWorldAsset().ToSoftObjectPath().ToString();
        }
        UE_LOG(LogSkald, Verbose,
               TEXT("BattleLevelManager: Restoring visibility for streaming level %s"),
               *LevelLabel);
      }
    }
  }
  HiddenStreamingLevels.Reset();

  if (HiddenPersistentLevel.IsValid()) {
    ULevel *PersistentLevel = HiddenPersistentLevel.Get();
    if (PersistentLevel) {
      if (!StreamingWorld) {
        StreamingWorld = PersistentLevel->GetWorld();
      }

      SetPersistentLevelVisibility(StreamingWorld, PersistentLevel,
                                   bPersistentLevelWasVisible);

      if (bPersistentLevelWasVisible) {
        UE_LOG(LogSkald, Verbose,
               TEXT("BattleLevelManager: Restored persistent level %s"),
               *PersistentLevel->GetOutermost()->GetName());
      }
    }
  }

  for (const FHiddenPersistentActorState &State : HiddenPersistentActors) {
    if (AActor *Actor = State.Actor.Get()) {
      Actor->SetActorHiddenInGame(State.bWasHiddenInGame);
      Actor->SetActorEnableCollision(State.bHadCollision);
      Actor->SetActorTickEnabled(State.bWasTickEnabled);
    }
  }

  HiddenPersistentActors.Reset();
  HiddenPersistentLevel.Reset();
  CachedStreamingWorld.Reset();
  bPersistentLevelWasVisible = false;
}

bool USkaldBattleLevelManager::IsStreamingLevelPartOfBattleMap(
    ULevelStreaming *Level) const {
  if (!Level) {
    return false;
  }

  if (ActiveStreamingLevel.IsValid() && Level == ActiveStreamingLevel.Get()) {
    return true;
  }

  const FString RequestedPackage =
      RequestedBattleLevel.ToSoftObjectPath().GetLongPackageName();
  if (RequestedPackage.IsEmpty()) {
    return false;
  }

  FString LevelPackage = ResolveStreamingLevelPackageName(Level);
  if (LevelPackage.IsEmpty()) {
    return false;
  }

  if (LevelPackage.Equals(RequestedPackage, ESearchCase::IgnoreCase)) {
    return true;
  }

  const FString RequestedPrefix = RequestedPackage + TEXT(".");
  if (LevelPackage.StartsWith(RequestedPrefix, ESearchCase::IgnoreCase)) {
    return true;
  }

  const FString RequestedShortName = FPackageName::GetShortName(RequestedPackage);
  if (RequestedShortName.IsEmpty()) {
    return false;
  }

  const FString LevelShortName = FPackageName::GetShortName(LevelPackage);
  if (LevelShortName.IsEmpty()) {
    return false;
  }

  if (LevelShortName.Equals(RequestedShortName, ESearchCase::IgnoreCase)) {
    return true;
  }

  const FString RequestedSubLevelPrefix =
      RequestedShortName + TEXT("_");
  if (LevelShortName.StartsWith(RequestedSubLevelPrefix, ESearchCase::IgnoreCase)) {
    return true;
  }

  return false;
}

bool USkaldBattleLevelManager::ResolveOrStreamLevel(
    UWorld* World, const TSoftObjectPtr<UWorld>& LevelAsset,
    ULevelStreaming*& OutStreamingLevel, const TCHAR* ContextLabel) {
  OutStreamingLevel = nullptr;
  if (!World || LevelAsset.IsNull()) {
    return false;
  }

  const FSoftObjectPath RequestedPath = LevelAsset.ToSoftObjectPath();
  const FString RequestedPackage = RequestedPath.GetLongPackageName();
  for (ULevelStreaming* ExistingLevel : World->GetStreamingLevels()) {
    if (!ExistingLevel) {
      continue;
    }

    const FString ExistingPackage = ResolveStreamingLevelPackageName(ExistingLevel);
    if (!RequestedPackage.IsEmpty() &&
        ExistingPackage.Equals(RequestedPackage, ESearchCase::IgnoreCase)) {
      OutStreamingLevel = ExistingLevel;
      UE_LOG(LogSkald, Log, TEXT("BattleLevelManager: Reusing %s streaming level %s"),
             ContextLabel, *ExistingPackage);
      return true;
    }
  }

  bool bLoadSuccess = false;
  OutStreamingLevel = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
      World, LevelAsset, FVector::ZeroVector, FRotator::ZeroRotator, bLoadSuccess);
  if (!OutStreamingLevel || !bLoadSuccess) {
    UE_LOG(LogSkald, Error, TEXT("BattleLevelManager: Failed to stream %s level %s"),
           ContextLabel, *LevelAsset.ToString());
    OutStreamingLevel = nullptr;
    return false;
  }

  UE_LOG(LogSkald, Log, TEXT("BattleLevelManager: Stream request issued for %s level %s"),
         ContextLabel, *LevelAsset.ToString());
  return true;
}

void USkaldBattleLevelManager::ApplyVisibilityForCurrentMode() {}
bool USkaldBattleLevelManager::IsStreamingLevelPartOfDiceBoard(ULevelStreaming* Level) const { return false; }
