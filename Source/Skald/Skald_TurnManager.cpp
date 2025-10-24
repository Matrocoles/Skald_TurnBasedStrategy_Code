#include "Skald_TurnManager.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "GridBattleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Net/UnrealNetwork.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "Skald_GameInstance.h"
#include "Skald_BattleLevelManager.h"
#include "Skald_BattleGameMode.h"
#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_PropertyAccess.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Territory.h"
#include "UI/SkaldMainHUDWidget.h"
#include "UObject/UnrealType.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/Package.h"
#include "TimerManager.h"
#include "WorldMap.h"
#include "Templates/Function.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

#if ENGINE_MAJOR_VERSION > 5 ||                                             \
    (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#else
#include "AssetRegistry/AssetRegistryTypes.h"
#endif

namespace {
constexpr float BattleResultReturnDelaySeconds = 5.0f;
FString GetWorldPackageName(const UWorld *World);
FString ResolveMapPackageFromRegistry(const FString &MapName);

FString GetFallbackOverviewMapPackageName()
{
  static const FString FallbackPath(TEXT("/Game/Blueprints/Maps/OverviewMap"));
  return FallbackPath;
}

FString StripStreamingPrefixFromPackageName(FString PackageName,
                                            const UWorld *ReferenceWorld) {
  if (PackageName.IsEmpty() || !ReferenceWorld) {
    return PackageName;
  }

  const FString Prefix = ReferenceWorld->StreamingLevelsPrefix;
  if (Prefix.IsEmpty()) {
    return PackageName;
  }

  auto StripPrefix = [&Prefix](FString &Value) {
    if (Value.StartsWith(Prefix)) {
      Value.RightChopInline(Prefix.Len(),
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
                             EAllowShrinking::No
#else
                             /*bAllowShrinking=*/false
#endif
      );

      if (Value.StartsWith(TEXT("_"))) {
        Value.RightChopInline(1,
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
                               EAllowShrinking::No
#else
                               /*bAllowShrinking=*/false
#endif
        );
      }
    }
  };

  FString Result = PackageName;

  if (FPackageName::IsValidLongPackageName(Result)) {
    const FString PathPart = FPackageName::GetLongPackagePath(Result);
    FString AssetPart = FPackageName::GetShortName(Result);
    if (!PathPart.IsEmpty() && !AssetPart.IsEmpty()) {
      FString TrimmedAsset = AssetPart;
      StripPrefix(TrimmedAsset);
      if (!TrimmedAsset.IsEmpty() && TrimmedAsset != AssetPart) {
        Result = PathPart + TEXT("/") + TrimmedAsset;
      }
      return Result;
    }
  }

  StripPrefix(Result);
  return Result;
}

FString EnsureLongPackageName(FString MapName, const UWorld *ReferenceWorld) {
  FString Result = MoveTemp(MapName);
  if (Result.IsEmpty()) {
    return Result;
  }

  int32 OptionsIndex = INDEX_NONE;
  if (Result.FindChar(TEXT('?'), OptionsIndex)) {
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
    Result.LeftInline(OptionsIndex, EAllowShrinking::No);
#else
    Result.LeftInline(OptionsIndex, /*bAllowShrinking=*/false);
#endif
  }

  if (Result.EndsWith(TEXT(".umap"))) {
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
    Result.LeftChopInline(5, EAllowShrinking::No);
#else
    Result.LeftChopInline(5, /*bAllowShrinking=*/false);
#endif
  }

  if (Result.Contains(TEXT("."))) {
    const FString PackageOnly = FPackageName::ObjectPathToPackageName(Result);
    if (!PackageOnly.IsEmpty()) {
      Result = PackageOnly;
    }
  }

  if (ReferenceWorld) {
    Result = StripStreamingPrefixFromPackageName(Result, ReferenceWorld);
  }

  if (!Result.IsEmpty() && FPackageName::IsShortPackageName(Result) &&
      ReferenceWorld) {
    FString ReferencePackage = GetWorldPackageName(ReferenceWorld);
    ReferencePackage = StripStreamingPrefixFromPackageName(ReferencePackage, ReferenceWorld);

    if (FPackageName::IsValidLongPackageName(ReferencePackage)) {
      const FString PathPart = FPackageName::GetLongPackagePath(ReferencePackage);
      if (!PathPart.IsEmpty()) {
        Result = PathPart + TEXT("/") + Result;
      }
    }
  }

  if (!Result.IsEmpty() && !FPackageName::IsValidLongPackageName(Result)) {
    FString Converted;
    if (FPackageName::TryConvertFilenameToLongPackageName(Result, Converted)) {
      Result = MoveTemp(Converted);
    }
  }

  if (!Result.IsEmpty() && !FPackageName::IsValidLongPackageName(Result)) {
    const FString RegistryName = ResolveMapPackageFromRegistry(Result);
    if (!RegistryName.IsEmpty()) {
      Result = RegistryName;
    }
  }

  if (ReferenceWorld && !Result.IsEmpty()) {
    Result = StripStreamingPrefixFromPackageName(Result, ReferenceWorld);
  }

  if (!Result.IsEmpty() && FPackageName::IsValidLongPackageName(Result)) {
    return Result;
  }

  return FString();
}

FString ResolveCanonicalReturnMapFromWorld(UWorld *World) {
  if (!World) {
    return FString();
  }

  struct FMapCandidate {
    FString Value;
    const TCHAR *Label;
  };

  TArray<FMapCandidate, TInlineAllocator<4>> Candidates;
  Candidates.Add({GetWorldPackageName(World), TEXT("world package")});
  Candidates.Add({World->URL.Map, TEXT("URL map")});
  Candidates.Add({UGameplayStatics::GetCurrentLevelName(World, false),
                  TEXT("current level (full)")});
  Candidates.Add({UGameplayStatics::GetCurrentLevelName(World, true),
                  TEXT("current level (stripped)")});

  for (const FMapCandidate &Candidate : Candidates) {
    FString Canonical = EnsureLongPackageName(Candidate.Value, World);
    if (!Canonical.IsEmpty()) {
      UE_LOG(LogSkald, Log,
             TEXT("ResolveCanonicalReturnMapFromWorld: %s candidate '%s' -> '%s'"),
             Candidate.Label, *Candidate.Value, *Canonical);
      return Canonical;
    }
  }

  UE_LOG(LogSkald, Error,
         TEXT("ResolveCanonicalReturnMapFromWorld: failed for world %s (URL=%s)"),
         *GetNameSafe(World), *World->URL.Map);
  return FString();
}

FString ResolveMapPackageFromRegistry(const FString &MapName) {
  if (MapName.IsEmpty()) {
    return FString();
  }

  FAssetRegistryModule *AssetRegistryModule =
      FModuleManager::LoadModulePtr<FAssetRegistryModule>("AssetRegistry");
  if (!AssetRegistryModule) {
    return FString();
  }

  IAssetRegistry &AssetRegistry = AssetRegistryModule->Get();
  AssetRegistry.WaitForCompletion();
  const FName TargetAssetName(*MapName);
  const FTopLevelAssetPath WorldClassPath =
      UWorld::StaticClass()->GetClassPathName();

  auto MatchesTarget = [&](const FAssetData &Asset) {
    return Asset.AssetName == TargetAssetName &&
           Asset.AssetClassPath == WorldClassPath;
  };

  FARFilter Filter;
  Filter.bRecursivePaths = true;
  Filter.ClassPaths.Add(WorldClassPath);
  Filter.PackagePaths.Add(FName(TEXT("/Game")));

  TArray<FAssetData> Assets;
  AssetRegistry.GetAssets(Filter, Assets);
  for (const FAssetData &Asset : Assets) {
    if (MatchesTarget(Asset)) {
      return Asset.PackageName.ToString();
    }
  }

  Assets.Reset();
  if (AssetRegistry.GetAllAssets(Assets, /*bIncludeOnlyOnDiskAssets=*/true)) {
    for (const FAssetData &Asset : Assets) {
      if (MatchesTarget(Asset)) {
        return Asset.PackageName.ToString();
      }
    }
  }

  return FString();
}

FString GetResolvedPlayerName(const ASkaldPlayerState *PlayerState,
                              const TCHAR *Context) {
  if (!PlayerState) {
    return TEXT("Unknown");
  }

  return PlayerState->GetResolvedPlayerName(Context);
}

FString GetWorldPackageName(const UWorld *World) {
  if (!World) {
    return FString();
  }

  const FSoftObjectPath WorldPath(World);
  const FString PathFromObject = WorldPath.GetLongPackageName();
  if (!PathFromObject.IsEmpty()) {
    return PathFromObject;
  }

  if (const ULevel *PersistentLevel = World->PersistentLevel) {
    if (const UPackage *Package = PersistentLevel->GetOutermost()) {
      return Package->GetPathName();
    }
  }

  if (const UPackage *WorldPackage = World->GetPackage()) {
    return WorldPackage->GetPathName();
  }

  return FString();
}

FString NormalizeMapName(UWorld *World, FString Candidate) {
  FString Result = MoveTemp(Candidate);

  if (Result.IsEmpty() && World) {
    Result = World->URL.Map;
    if (Result.IsEmpty()) {
      Result = GetWorldPackageName(World);
    }
  }

  if (!Result.IsEmpty()) {
    int32 OptionsIndex = INDEX_NONE;
    if (Result.FindChar(TEXT('?'), OptionsIndex)) {
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
      Result.LeftInline(OptionsIndex, EAllowShrinking::No);
#else
      Result.LeftInline(OptionsIndex, /*bAllowShrinking=*/false);
#endif
    }

    if (World && !World->StreamingLevelsPrefix.IsEmpty() &&
        Result.StartsWith(World->StreamingLevelsPrefix)) {
      Result.RightChopInline(World->StreamingLevelsPrefix.Len(),
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
                             EAllowShrinking::No);
#else
                             /*bAllowShrinking=*/false);
#endif

      if (Result.StartsWith(TEXT("_"))) {
        Result.RightChopInline(1,
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
                              EAllowShrinking::No);
#else
                              /*bAllowShrinking=*/false);
#endif
      }
    }

    if (FPackageName::IsShortPackageName(Result)) {
      FString LongPackageName;
      if (FPackageName::SearchForPackageOnDisk(Result, &LongPackageName)) {
        Result = MoveTemp(LongPackageName);
      } else {
        const FString RegistryName = ResolveMapPackageFromRegistry(Result);
        if (!RegistryName.IsEmpty()) {
          Result = RegistryName;
        }
      }
    } else {
      FSoftObjectPath MapPath(Result);
      if (MapPath.IsValid()) {
        const FString LongPackageName = MapPath.GetLongPackageName();
        if (!LongPackageName.IsEmpty()) {
          Result = LongPackageName;
        }
      } else if (Result.Contains(TEXT("."))) {
        const FString ObjectPathPackage =
            FPackageName::ObjectPathToPackageName(Result);
        if (!ObjectPathPackage.IsEmpty()) {
          Result = ObjectPathPackage;
        }
      } else if (!FPackageName::IsValidLongPackageName(Result)) {
        FString ConvertedName;
        if (FPackageName::TryConvertFilenameToLongPackageName(Result,
                                                             ConvertedName)) {
          Result = MoveTemp(ConvertedName);
        }
      }
    }
  }

  if (!Result.IsEmpty() && !FPackageName::IsValidLongPackageName(Result)) {
    FString ConvertedName;
    if (FPackageName::TryConvertFilenameToLongPackageName(Result, ConvertedName)) {
      Result = MoveTemp(ConvertedName);
    } else {
      const FSoftObjectPath ObjectPath(Result);
      const FString LongFromObject = ObjectPath.GetLongPackageName();
      if (!LongFromObject.IsEmpty()) {
        Result = LongFromObject;
      }
    }
  }

  if (!Result.IsEmpty() && !FPackageName::IsValidLongPackageName(Result) &&
      FPackageName::IsShortPackageName(Result)) {
    const FString RegistryName = ResolveMapPackageFromRegistry(Result);
    if (!RegistryName.IsEmpty()) {
      Result = RegistryName;
    }
  }

  return Result;
}
} // namespace

using Skald::PropertyAccess::ReadBoolProperty;
using Skald::PropertyAccess::ReadIntProperty;

ATurnManager::ATurnManager() {
  PrimaryActorTick.bCanEverTick = false;
  bReplicates = true;
  CurrentIndex = 0;
  CachedWorldMap = nullptr;
}

void ATurnManager::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);
  DOREPLIFETIME(ATurnManager, BattleMaps);
  DOREPLIFETIME(ATurnManager, BattleMapEntries);
}

void ATurnManager::BeginPlay() {
  Super::BeginPlay();

  CachedWorldMap = ResolveWorldMap();

  const bool bOnWorldMap = (CachedWorldMap != nullptr);

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->OnBattleMapStateChanged.RemoveDynamic(
        this, &ATurnManager::HandleBattleMapStateChanged);
    GI->OnBattleMapStateChanged.AddDynamic(
        this, &ATurnManager::HandleBattleMapStateChanged);

    const bool bHasPendingResolution =
        GI->bPendingBattleResolution || GI->PendingBattleResolution.bValid;

    // If a battle finished while we were away, make sure we resolve it even if
    // the world map actors are still streaming in. ResolveGridBattleResult
    // already handles deferring until the map is ready, so we only need to
    // trigger it here when we know a resolution is pending.
    if (bHasPendingResolution) {
      ResolveGridBattleResult();
    }

    // On the battle map, listen for battle end and travel back on event
    if (!bOnWorldMap) {
      AttemptBindBattleEnd(GI);
    }

  }

  if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
    if (!GM->IsWorldInitialized()) {
      GM->TryInitializeWorldAndStart();
    }
  }
}

void ATurnManager::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->OnBattleMapStateChanged.RemoveDynamic(
        this, &ATurnManager::HandleBattleMapStateChanged);
    ClearBattleEndBinding(GI);
  } else if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(BattleEndBindingRetryHandle);
  }

  if (UWorld *World = GetWorld()) {
    FTimerManager &TimerManager = World->GetTimerManager();
    TimerManager.ClearTimer(BattleReturnDelayHandle);
    TimerManager.ClearTimer(BattleEndBindingRetryHandle);
    TimerManager.ClearTimer(PhaseBroadcastRetryHandle);
  }
  bPhaseBroadcastRetryActive = false;
  bBattleReturnPending = false;

  Super::EndPlay(EndPlayReason);
}

void ATurnManager::HandleGridBattleEnded(ESkaldFaction /*WinningFaction*/, int32 /*AttackerCasualties*/, int32 /*DefenderCasualties*/) {
  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (GI) {
    if (!GI->bIsInBattleMap) {
      UE_LOG(LogSkald, Verbose,
             TEXT("HandleGridBattleEnded ignored: battle map already inactive."));
      return;
    }

    ClearBattleEndBinding(GI);
  }

  if (bBattleReturnPending) {
    return;
  }

  UWorld *World = GetWorld();
  if (World) {
    World->GetTimerManager().ClearTimer(BattleReturnDelayHandle);
  }

  bBattleReturnPending = true;

  if (World && BattleResultReturnDelaySeconds > 0.f) {
    World->GetTimerManager().SetTimer(
        BattleReturnDelayHandle, this, &ATurnManager::CompleteBattleConclusion,
        BattleResultReturnDelaySeconds, false);
  } else {
    CompleteBattleConclusion();
  }
}

void ATurnManager::CompleteBattleConclusion() {
  UWorld *World = GetWorld();
  if (World) {
    World->GetTimerManager().ClearTimer(BattleReturnDelayHandle);
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (GI && !GI->bIsInBattleMap) {
    UE_LOG(LogSkald, Verbose,
           TEXT("CompleteBattleConclusion aborted: battle map already inactive."));
    bBattleReturnPending = false;
    return;
  }

  if (GI && GI->bTravelPending) {
    UE_LOG(LogSkald, Warning,
           TEXT("CompleteBattleConclusion aborted: travel already pending."));
    bBattleReturnPending = false;
    return;
  }

  FString ReturnMapName;
  FString ReturnMapSource;

  auto TryResolveReturnMap = [&](const FString &Candidate,
                                 const TCHAR *SourceLabel) {
    if (Candidate.IsEmpty()) {
      return false;
    }

    FString Canonical = EnsureLongPackageName(Candidate, nullptr);
    if (Canonical.IsEmpty() && World) {
      Canonical = EnsureLongPackageName(Candidate, World);
    }
    if (Canonical.IsEmpty()) {
      UE_LOG(LogSkald, Warning,
             TEXT("HandleGridBattleEnded: %s value '%s' is not a valid long package name."),
             SourceLabel, *Candidate);
      return false;
    }

    ReturnMapName = MoveTemp(Canonical);
    ReturnMapSource = SourceLabel;
    return true;
  };

  if (!TryResolveReturnMap(PendingBattle.ReturnMap,
                           TEXT("PendingBattle.ReturnMap")) &&
      GI) {
    if (!TryResolveReturnMap(GI->PendingBattle.ReturnMap,
                             TEXT("GameInstance.PendingBattle.ReturnMap"))) {
      TryResolveReturnMap(GI->GetPendingReturnMap(),
                          TEXT("GameInstance.PendingReturnMap"));
    }
  }

  if (ReturnMapName.IsEmpty()) {
    const FString FallbackMap = GetFallbackOverviewMapPackageName();
    if (TryResolveReturnMap(FallbackMap, TEXT("FallbackOverviewMap"))) {
      if (GI) {
        GI->SetPendingReturnMap(ReturnMapName);
      }
    } else {
      const FString PendingBattleValue = PendingBattle.ReturnMap;
      const FString GameInstanceValue = GI ? GI->PendingBattle.ReturnMap : FString();
      const TCHAR *PendingValueForLog =
          PendingBattleValue.IsEmpty() ? TEXT("<Empty>") : *PendingBattleValue;
      const TCHAR *GameInstanceValueForLog =
          GameInstanceValue.IsEmpty() ? TEXT("<Empty>") : *GameInstanceValue;
      UE_LOG(LogSkald, Error,
             TEXT("HandleGridBattleEnded: unable to resolve return map (Pending='%s', GI='%s')."),
             PendingValueForLog, GameInstanceValueForLog);
      bBattleReturnPending = false;
      return;
    }
  }

  UE_LOG(LogSkald, Verbose,
         TEXT("HandleGridBattleEnded: resolved return map '%s' from %s."),
         *ReturnMapName, *ReturnMapSource);

  ResolveGridBattleResult();

  if (GI) {
    GI->SetTravelPending(true);
  }

  if (!World) {
    World = GetWorld();
  }

  if (World) {
    const ENetMode NetMode = World->GetNetMode();
    auto NetModeToString = [](ENetMode InNetMode) {
      switch (InNetMode) {
      case NM_Standalone:
        return TEXT("NM_Standalone");
      case NM_DedicatedServer:
        return TEXT("NM_DedicatedServer");
      case NM_ListenServer:
        return TEXT("NM_ListenServer");
      case NM_Client:
        return TEXT("NM_Client");
      case NM_MAX:
        return TEXT("NM_MAX");
      default:
        return TEXT("Unknown");
      }
    };
    const TCHAR *NetModeStringPtr = NetModeToString(NetMode);

    UE_LOG(LogSkald, Log,
           TEXT("HandleGridBattleEnded: travelling to '%s' (source=%s, NetMode=%s)."),
           *ReturnMapName, *ReturnMapSource,
           NetModeStringPtr);

    switch (NetMode) {
    case NM_Standalone: {
      const FName LevelName(*ReturnMapName);
      UGameplayStatics::OpenLevel(World, LevelName, /*bAbsolute=*/true);
      break;
    }
    case NM_DedicatedServer:
    case NM_ListenServer: {
      FString ListenTarget = ReturnMapName;
      if (!ListenTarget.Contains(TEXT("?listen"))) {
        ListenTarget.Append(TEXT("?listen"));
      }
      World->ServerTravel(ListenTarget);
      break;
    }
    case NM_Client:
      // Clients should wait for the server's travel notification instead of
      // loading the map locally with an incomplete URL. The pending server
      // travel initiated above will automatically move connected clients.
      break;
    default:
      World->ServerTravel(ReturnMapName);
      break;
    }
  }

  bBattleReturnPending = false;
}

void ATurnManager::HandleBattleMapStateChanged(bool bInBattleMap) {
  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    if (bInBattleMap) {
      AttemptBindBattleEnd(GI);
    } else {
      ClearBattleEndBinding(GI);
    }
  }
}

void ATurnManager::AttemptBindBattleEnd(USkaldGameInstance *GameInstance,
                                        int32 Attempt) {
  if (!GameInstance || !GameInstance->bIsInBattleMap) {
    return;
  }

  UWorld *World = GetWorld();
  if (World) {
    World->GetTimerManager().ClearTimer(BattleEndBindingRetryHandle);
  }

  UGridBattleManager *BattleManager = GameInstance->GridBattleManager;
  if (BattleManager) {
    if (UGridBattleManager *PreviouslyBound = BoundBattleManager.Get()) {
      if (PreviouslyBound != BattleManager) {
        PreviouslyBound->OnBattleEnded.RemoveDynamic(
            this, &ATurnManager::HandleGridBattleEnded);
      }
    }

    if (!BattleManager->OnBattleEnded.IsAlreadyBound(
            this, &ATurnManager::HandleGridBattleEnded)) {
      BattleManager->OnBattleEnded.AddDynamic(
          this, &ATurnManager::HandleGridBattleEnded);
    }

    BoundBattleManager = BattleManager;

    if (World) {
      constexpr float RebindPollSeconds = 0.1f;
      FTimerDelegate MonitorDelegate = FTimerDelegate::CreateWeakLambda(
          this, [this]() {
            if (USkaldGameInstance *RetryGI =
                    GetGameInstance<USkaldGameInstance>()) {
              if (RetryGI->bIsInBattleMap) {
                AttemptBindBattleEnd(RetryGI);
              }
            }
          });
      World->GetTimerManager().SetTimer(
          BattleEndBindingRetryHandle, MonitorDelegate, RebindPollSeconds,
          false);
    }
    return;
  }

  if (!World) {
    return;
  }

  constexpr int32 MaxAttempts = 40;
  if (Attempt >= MaxAttempts) {
    UE_LOG(LogSkald, Warning,
           TEXT("TurnManager could not bind battle end delegate after %d attempts."),
           Attempt);
    return;
  }

  constexpr float RetryDelaySeconds = 0.05f;
  FTimerDelegate RetryDelegate = FTimerDelegate::CreateWeakLambda(
      this, [this, Attempt]() {
        if (USkaldGameInstance *RetryGI =
                GetGameInstance<USkaldGameInstance>()) {
          AttemptBindBattleEnd(RetryGI, Attempt + 1);
        }
      });
  World->GetTimerManager().SetTimer(BattleEndBindingRetryHandle, RetryDelegate,
                                    RetryDelaySeconds, false);
}

void ATurnManager::ClearBattleEndBinding(USkaldGameInstance *GameInstance) {
  if (!GameInstance) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(BattleEndBindingRetryHandle);
  }

  if (GameInstance->GridBattleManager) {
    GameInstance->GridBattleManager->OnBattleEnded.RemoveDynamic(
        this, &ATurnManager::HandleGridBattleEnded);
  }

  if (UGridBattleManager *PreviouslyBound = BoundBattleManager.Get()) {
    if (PreviouslyBound != GameInstance->GridBattleManager) {
      PreviouslyBound->OnBattleEnded.RemoveDynamic(
          this, &ATurnManager::HandleGridBattleEnded);
    }
  }

  BoundBattleManager = nullptr;
}

void ATurnManager::RegisterController(ASkaldPlayerController *Controller) {
  RegisterControllerInternal(Controller, /*bSuppressPreparePrompt=*/false);
}

void ATurnManager::RegisterControllerInternal(
    ASkaldPlayerController *Controller, bool bSuppressPreparePrompt) {
  if (IsValid(Controller) && !Controllers.Contains(Controller)) {
    Controllers.Add(Controller);
    Controller->SetTurnManager(this);

    if (!bSuppressPreparePrompt) {
      MaybePromptPendingBattleParticipant(Controller);
    }
  }
}

bool ATurnManager::AttemptResumeSavedTurnState() {
  return TryResumeSavedTurnState();
}

void ATurnManager::StartArmyPlacementPhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending || GI->bResumeTurns) {
        return;
      }
    }
  }

  CurrentPhase = ETurnPhase::ArmyPlacement;
  CurrentIndex = 0;
  bHasTurnsStarted = false;
  BroadcastCurrentPhase();
}

void ATurnManager::ApplyReinforcementsAndResources(ASkaldPlayerState *PS,
                                                   const TCHAR *Caller) {
  if (!PS) {
    return;
  }
  int32 Owned = 0;
  int32 ResourceGain = 0;
  if (AWorldMap *WorldMap = ResolveWorldMap()) {
    if (WorldMap->Territories.Num() == 0) {
      UE_LOG(LogSkald, Error, TEXT("%s: WorldMap %s has no territories"),
             Caller, *WorldMap->GetName());
      if (GEngine) {
        GEngine->AddOnScreenDebugMessage(
            -1, 5.f, FColor::Red,
            FString::Printf(TEXT("%s: %s has no territories"), Caller,
                            *WorldMap->GetName()));
      }
    } else {
      for (ATerritory *Terr : WorldMap->Territories) {
        if (Terr && Terr->OwningPlayer == PS) {
          ++Owned;
          ResourceGain += Terr->Resources;
        }
      }
    }
  } else {
    UE_LOG(LogSkald, Error, TEXT("%s: WorldMap actor missing"), Caller);
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          FString::Printf(TEXT("%s: WorldMap missing"), Caller));
    }
  }
  const int32 Reinforcements = FMath::CeilToInt(Owned / 3.0f);
  PS->DeployableUnits += Reinforcements;
  PS->Resources += ResourceGain;
  BroadcastDeployableUnits(PS);
  BroadcastResources(PS);
}

/** Internal: set GameState.CurrentTurnIndex (and broadcast) to match
 * CurrentIndex. */
void ATurnManager::SyncGameStateTurnIndex() {
  if (ASkaldGameState *GS = GetWorld()->GetGameState<ASkaldGameState>()) {
    int32 NewIndex = -1;
    if (Controllers.IsValidIndex(CurrentIndex) &&
        Controllers[CurrentIndex].IsValid()) {
      if (ASkaldPlayerState *PS =
              Controllers[CurrentIndex]->GetPlayerState<ASkaldPlayerState>()) {
        NewIndex = GS->PlayerArray.IndexOfByKey(PS);
      }
    }
    GS->CurrentTurnIndex = NewIndex;
    GS->OnTurnIndexChanged.Broadcast(NewIndex);
  }
}

bool ATurnManager::TryResumeSavedTurnState(USkaldGameInstance *GameInstance) {
  USkaldGameInstance *GI =
      GameInstance ? GameInstance : GetGameInstance<USkaldGameInstance>();
  if (!GI || !GI->bResumeTurns) {
    return false;
  }

  const int32 SavedIndex = GI->SavedTurnIndex;
  const int32 SavedPlayerId = GI->SavedTurnPlayerId;

  auto ResolveIndexForPlayerId = [&](int32 PlayerId) -> int32 {
    if (PlayerId <= 0) {
      return INDEX_NONE;
    }

    for (int32 Index = 0; Index < Controllers.Num(); ++Index) {
      if (!Controllers[Index].IsValid()) {
        continue;
      }
      if (ASkaldPlayerController *Candidate = Controllers[Index].Get()) {
        if (ASkaldPlayerState *PS =
                Candidate->GetPlayerState<ASkaldPlayerState>()) {
          if (PS->GetPlayerId() == PlayerId) {
            return Index;
          }
        }
      }
    }
    return INDEX_NONE;
  };

  int32 TargetIndex = ResolveIndexForPlayerId(SavedPlayerId);
  if (!Controllers.IsValidIndex(TargetIndex) || !Controllers[TargetIndex].IsValid()) {
    TargetIndex = SavedIndex;
  }

  if (!Controllers.IsValidIndex(TargetIndex) || !Controllers[TargetIndex].IsValid()) {
    return false;
  }

  ASkaldPlayerController *Controller = Controllers[TargetIndex].Get();
  if (!Controller) {
    return false;
  }

  CurrentIndex = TargetIndex;
  CurrentPhase = GI->SavedTurnPhase;
  GI->bResumeTurns = false;
  GI->SavedTurnPlayerId = 0;
  GI->SavedTurnIndex = TargetIndex;

  SyncGameStateTurnIndex();
  Controller->StartTurn();
  bHasTurnsStarted = true;
  const bool bBroadcasted = BroadcastCurrentPhase();
  if (!bBroadcasted) {
    QueuePhaseBroadcastRetry(CurrentPhase);
  }

  return true;
}

void ATurnManager::StartTurns(ASkaldPlayerController *StartingController) {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  SortControllersByInitiative();

  if (Controllers.Num() == 0) {
    UE_LOG(LogSkald, Error,
           TEXT("StartTurns failed: no controllers registered"));
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red,
                                       TEXT("StartTurns failed: no players"));
    }
    return;
  }

  int32 StartIndex = INDEX_NONE;
  if (StartingController) {
    StartIndex = Controllers.IndexOfByPredicate(
        [StartingController](const TWeakObjectPtr<ASkaldPlayerController> &Ptr) {
          return Ptr.Get() == StartingController;
        });
  }

  if (StartIndex == INDEX_NONE) {
    for (int32 i = 0; i < Controllers.Num(); ++i) {
      if (Controllers[i].IsValid()) {
        StartIndex = i;
        break;
      }
    }
  }

  if (StartIndex == INDEX_NONE || !Controllers.IsValidIndex(StartIndex) ||
      !Controllers[StartIndex].IsValid()) {
    UE_LOG(LogSkald, Error,
           TEXT("StartTurns failed: could not find a valid starting controller"));
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red,
                                       TEXT("StartTurns: no valid starting player"));
    }
    return;
  }

  CurrentIndex = StartIndex;

  ASkaldPlayerController *CurrentController = Controllers[CurrentIndex].Get();
  if (!CurrentController) {
    UE_LOG(LogSkald, Error,
           TEXT("StartTurns failed: starting controller pointer invalid"));
    return;
  }

  ASkaldPlayerState *PS = CurrentController->GetPlayerState<ASkaldPlayerState>();
  const FString PlayerName =
      GetResolvedPlayerName(PS, TEXT("StartTurns_Current"));
  ApplyReinforcementsAndResources(PS, TEXT("StartTurns"));

  CurrentPhase = ETurnPhase::Reinforcement;
  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr :
       Controllers) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      const bool bIsActive = Controller == CurrentController;
      Controller->ShowTurnAnnouncement(PlayerName, bIsActive);
      ASkaldPlayerState *ControllerPS =
          Controller->GetPlayerState<ASkaldPlayerState>();
      const bool bIsAI = ControllerPS && ControllerPS->bIsAI;
      if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
        HUD->UpdateTurnBanner(PS ? PS->GetPlayerId() : -1, 1);
        HUD->UpdatePhaseBanner(CurrentPhase);
      } else if (!bIsAI && Controller->IsLocalController()) {
        UE_LOG(LogSkald, Warning,
               TEXT("StartTurns: Controller %s missing HUD widget"),
               *Controller->GetName());
        if (GEngine) {
          GEngine->AddOnScreenDebugMessage(
              -1, 5.f, FColor::Yellow,
              FString::Printf(TEXT("StartTurns: no HUD for %s"),
                              *Controller->GetName()));
        }
      }
    }
  }

  SyncGameStateTurnIndex();
  CurrentController->StartTurn();
  bHasTurnsStarted = true;
  if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
    GM->CheckVictoryConditions();
  }

  OnWorldStateChanged.Broadcast();
}

void ATurnManager::AdvanceTurn() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  ASkaldPlayerController *PreviousController =
      Controllers.IsValidIndex(CurrentIndex) ? Controllers[CurrentIndex].Get()
                                             : nullptr;
  FString PreviousPlayerName;
  if (PreviousController) {
    if (ASkaldPlayerState *PrevPS =
            PreviousController->GetPlayerState<ASkaldPlayerState>()) {
      PreviousPlayerName =
          GetResolvedPlayerName(PrevPS, TEXT("AdvanceTurn_Previous"));
    }
  }

  Controllers.RemoveAll([](const TWeakObjectPtr<ASkaldPlayerController> &Ptr) {
    if (!Ptr.IsValid()) {
      return true;
    }
    if (ASkaldPlayerController *PC = Ptr.Get()) {
      if (ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>()) {
        return PS->IsEliminated;
      }
    }
    return false;
  });
  if (Controllers.Num() == 0) {
    return;
  }

  AWorldMap *WorldMap = ResolveWorldMap();
  if (!WorldMap || WorldMap->Territories.Num() == 0) {
    UE_LOG(LogSkald, Warning,
           TEXT("AdvanceTurn aborted: WorldMap missing or has no territories"));
    return;
  }

  int32 FoundIndex = Controllers.IndexOfByPredicate(
      [PreviousController](const TWeakObjectPtr<ASkaldPlayerController> &Ptr) {
        return Ptr.Get() == PreviousController;
      });
  CurrentIndex =
      (FoundIndex != INDEX_NONE) ? FoundIndex : Controllers.Num() - 1;

  CurrentIndex = (CurrentIndex + 1) % Controllers.Num();
  if (ASkaldPlayerController *CurrentController =
          Controllers[CurrentIndex].Get()) {
    ASkaldPlayerState *PS =
        CurrentController->GetPlayerState<ASkaldPlayerState>();
    const FString PlayerName =
        GetResolvedPlayerName(PS, TEXT("AdvanceTurn_Current"));
    ApplyReinforcementsAndResources(PS, TEXT("AdvanceTurn"));

    CurrentPhase = ETurnPhase::Reinforcement;
    for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr :
         Controllers) {
      if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
        const bool bIsActive = Controller == CurrentController;
        Controller->NotifyTurnEnded(PreviousPlayerName);
        Controller->ShowTurnAnnouncement(PlayerName, bIsActive);
        if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
          HUD->UpdateTurnBanner(PS ? PS->GetPlayerId() : -1, 1);
          HUD->UpdatePhaseBanner(CurrentPhase);
        }
      }
    }

    CurrentController->StartTurn();
    SyncGameStateTurnIndex();
    if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
      GM->CheckVictoryConditions();
    }
  }

  OnWorldStateChanged.Broadcast();
}

void ATurnManager::SortControllersByInitiative() {
  Controllers.RemoveAll([](const TWeakObjectPtr<ASkaldPlayerController> &Ptr) {
    return !Ptr.IsValid();
  });
  Controllers.Sort([](const TWeakObjectPtr<ASkaldPlayerController> &A,
                      const TWeakObjectPtr<ASkaldPlayerController> &B) {
    const ASkaldPlayerState *PSA =
        A.IsValid() ? A->GetPlayerState<ASkaldPlayerState>() : nullptr;
    const ASkaldPlayerState *PSB =
        B.IsValid() ? B->GetPlayerState<ASkaldPlayerState>() : nullptr;
    const int32 RollA = PSA ? PSA->InitiativeRoll : 0;
    const int32 RollB = PSB ? PSB->InitiativeRoll : 0;
    return RollA > RollB;
  });
}

TArray<ASkaldPlayerController *> ATurnManager::GetControllers() const {
  TArray<ASkaldPlayerController *> Result;
  for (const TWeakObjectPtr<ASkaldPlayerController> &Ptr : Controllers) {
    if (Ptr.IsValid()) {
      Result.Add(Ptr.Get());
    }
  }
  return Result;
}

void ATurnManager::RequestPrepareBattle(const FS_BattlePayload &Battle) {
  PendingBattlePreparation = Battle;
  PendingBattleReadyState = FPendingBattleReadyState();
  PendingBattleReadyState.AttackerPlayerID = Battle.AttackerPlayerID;
  PendingBattleReadyState.DefenderPlayerID = Battle.DefenderPlayerID;
  PendingBattleReadyState.bAttackerReady =
      (Battle.AttackerPlayerID == INDEX_NONE) || Battle.bAttackerIsAI;
  PendingBattleReadyState.bDefenderReady =
      (Battle.DefenderPlayerID == INDEX_NONE) || Battle.bDefenderIsAI;

  EnsureBattleParticipantsRegistered();
  BroadcastPrepareForBattlePrompt(Battle);
  TryLaunchPreparedBattle();
}

void ATurnManager::TriggerGridBattle(const FS_BattlePayload &Battle) {
  FS_BattlePayload SeededBattle = Battle;
  SeededBattle.RandomSeed = FMath::Rand();

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (GI && (GI->bTravelPending || GI->bPendingBattleResolution)) {
    UE_LOG(LogSkald, Verbose,
           TEXT("TriggerGridBattle deferred: travel/resolution in progress (TravelPending=%s PendingResolution=%s)"),
           GI->bTravelPending ? TEXT("true") : TEXT("false"),
           (GI->bPendingBattleResolution && GI->PendingBattleResolution.bValid)
               ? TEXT("true")
               : TEXT("false"));

    DeferredPendingBattle = SeededBattle;

    if (UWorld *World = GetWorld()) {
      if (!World->GetTimerManager().IsTimerActive(PendingBattleTravelRetryHandle)) {
        FTimerDelegate RetryDelegate =
            FTimerDelegate::CreateUObject(this, &ATurnManager::RetryPendingBattleTravel);
        constexpr float RetryDelaySeconds = 0.1f;
        World->GetTimerManager().SetTimer(PendingBattleTravelRetryHandle, RetryDelegate,
                                          RetryDelaySeconds, false);
      }
    }
    return;
  }

  bool bGameModeHasSnapshotAfterCall = GI && GI->CachedWorldMapTerritories.Num() > 0;

  if (UWorld *ExistingWorld = GetWorld()) {
    ExistingWorld->GetTimerManager().ClearTimer(PendingBattleTravelRetryHandle);
  }

  if (UWorld *World = GetWorld()) {
    SeededBattle.ReturnMap = ResolveCanonicalReturnMapFromWorld(World);

    if (SeededBattle.ReturnMap.IsEmpty()) {
      const FString FallbackMap = GetFallbackOverviewMapPackageName();
      UE_LOG(LogSkald, Error,
             TEXT("TriggerGridBattle: Failed to resolve a canonical return map for world %s. Falling back to '%s'."),
             *GetNameSafe(World), *FallbackMap);
      SeededBattle.ReturnMap = FallbackMap;
      if (GI) {
        GI->SetPendingReturnMap(FallbackMap);
      }
    } else if (!FPackageName::IsValidLongPackageName(SeededBattle.ReturnMap)) {
      UE_LOG(LogSkald, Warning,
             TEXT("TriggerGridBattle resolved return map '%s' which is not a valid long package name."),
             *SeededBattle.ReturnMap);
    } else {
      UE_LOG(LogSkald, Log,
             TEXT("TriggerGridBattle: storing return map '%s' before travelling to battle."),
             *SeededBattle.ReturnMap);
      if (GI) {
        GI->SetPendingReturnMap(SeededBattle.ReturnMap);
      }
    }

    CachedWorldMap = ResolveWorldMap();

    if (GI) {
      GI->CacheWorldMapSnapshot(World);
      bGameModeHasSnapshotAfterCall =
          (GI->CachedWorldMapTerritories.Num() > 0);
    } else {
      bGameModeHasSnapshotAfterCall = false;
    }
  }
  PendingBattle = SeededBattle;
  DeferredPendingBattle = FS_BattlePayload();

  if (GI) {
    GI->SeedCombatRandomStream(SeededBattle.RandomSeed);
    GI->PendingBattleResolution = FGridBattleResolution();
    GI->bPendingBattleResolution = false;
    if (!GI->GridBattleManager) {
      GI->GridBattleManager = NewObject<UGridBattleManager>(GI);
    }
  }

  // Save the current turn state so it can be restored after travelling.
  if (GI) {
    int32 SavedPlayerId = 0;
    if (Controllers.IsValidIndex(CurrentIndex) &&
        Controllers[CurrentIndex].IsValid()) {
      if (ASkaldPlayerState *PS =
              Controllers[CurrentIndex]->GetPlayerState<ASkaldPlayerState>()) {
        SavedPlayerId = PS->GetPlayerId();
      }
    }

    GI->SavedTurnIndex = CurrentIndex;
    GI->SavedTurnPlayerId = SavedPlayerId;

    ETurnPhase PhaseToResume = CurrentPhase;
    if (PhaseToResume != ETurnPhase::Attack) {
      UE_LOG(LogSkald, Verbose,
             TEXT("TriggerGridBattle: forcing resume phase to Attack (was %s)."),
             *UEnum::GetValueAsString(PhaseToResume));
      PhaseToResume = ETurnPhase::Attack;
      CurrentPhase = ETurnPhase::Attack;
    }

    GI->SavedTurnPhase = PhaseToResume;
    GI->bResumeTurns = true;
  }

  // Load a battle map where the grid based combat takes place.
  if (UWorld *World = GetWorld()) {
    if (!GI) {
      GI = GetGameInstance<USkaldGameInstance>();
    }

    bool bShouldStreamSelectedMap = true;
    TSoftObjectPtr<UWorld> SelectedBattleMap;
    if (BattleMapEntries.Num() > 0) {
      const int32 Index = FMath::RandRange(0, BattleMapEntries.Num() - 1);
      const FBattleMapDescriptor &Entry = BattleMapEntries[Index];
      SelectedBattleMap = Entry.Map;
      bShouldStreamSelectedMap = Entry.bStreamAsSubLevel;
    } else if (BattleMaps.Num() > 0) {
      const int32 Index = FMath::RandRange(0, BattleMaps.Num() - 1);
      SelectedBattleMap = BattleMaps[Index];
      bShouldStreamSelectedMap = true;
    }
    if (SelectedBattleMap.IsNull()) {
      SelectedBattleMap = TSoftObjectPtr<UWorld>(
          FSoftObjectPath(TEXT("/Game/Blueprints/Maps/BattleMap.BattleMap")));
      bShouldStreamSelectedMap = true;
    }

    FString MapToLoad =
        SelectedBattleMap.ToSoftObjectPath().GetLongPackageName();
    if (MapToLoad.IsEmpty()) {
      MapToLoad = TEXT("/Game/Blueprints/Maps/BattleMap");
    }

    ASkaldGameState *GS = World->GetGameState<ASkaldGameState>();

    FSkaldTravelState TravelState;
    int32 ValidControllers = 0;
    for (const TWeakObjectPtr<ASkaldPlayerController> &Ptr : Controllers) {
      if (Ptr.IsValid()) {
        ++ValidControllers;
      }
    }
    TravelState.ExpectedControllers = ValidControllers;
    TravelState.AttackerTerritory = SeededBattle.FromTerritoryID;
    TravelState.DefenderTerritory = SeededBattle.TargetTerritoryID;
    TravelState.ReturnMap = ResolveCanonicalReturnMapFromWorld(World);

    AWorldMap *WorldMap = ResolveWorldMap();
    TArray<FS_Territory> TerritorySnapshots;
    bool bUsedCachedFallback = false;
    TSet<int32> HumanOwnedTerritoryIds;
    bool bCapturedFromLiveWorld =
        CaptureWorldSnapshot(TerritorySnapshots, &HumanOwnedTerritoryIds);

    if (bCapturedFromLiveWorld) {
      TravelState.HumanOwnedTerritories.Reserve(
          TravelState.HumanOwnedTerritories.Num() +
          HumanOwnedTerritoryIds.Num());
      for (int32 TerritoryId : HumanOwnedTerritoryIds) {
        TravelState.HumanOwnedTerritories.AddUnique(TerritoryId);
      }
    }

    const bool bHasFallbackSnapshot =
        GI && GI->CachedWorldMapTerritories.Num() > 0;

    if (!bCapturedFromLiveWorld && bHasFallbackSnapshot) {
      bUsedCachedFallback = true;
      TerritorySnapshots = GI->CachedWorldMapTerritories;
      for (const FS_Territory &Snapshot : TerritorySnapshots) {
        if (Snapshot.OwnerPlayerID > 0 && GS) {
          if (ASkaldPlayerState *OwnerPS = GS->GetPlayerById(Snapshot.OwnerPlayerID)) {
            if (OwnerPS && !OwnerPS->bIsAI && Snapshot.TerritoryID > 0) {
              TravelState.HumanOwnedTerritories.AddUnique(Snapshot.TerritoryID);
            }
          }
        }
      }
      bCapturedFromLiveWorld = false;
    }

    if (TerritorySnapshots.Num() == 0) {
      UE_LOG(LogSkald, Warning,
             TEXT("TriggerGridBattle could not capture a territory snapshot (fallbackUsed=%d)"),
             bUsedCachedFallback ? 1 : 0);
    } else {
      UE_LOG(LogSkald, Verbose,
             TEXT("TriggerGridBattle captured %d territory snapshots (fallbackUsed=%d)"),
             TerritorySnapshots.Num(), bUsedCachedFallback ? 1 : 0);
    }

    TArray<FS_Territory> TravelSnapshots = TerritorySnapshots;
    if (bCapturedFromLiveWorld && GI) {
      const int32 NewSnapshotCount = TerritorySnapshots.Num();
      if (!bGameModeHasSnapshotAfterCall && NewSnapshotCount > 0) {
        UE_LOG(LogSkald, Warning,
               TEXT("TriggerGridBattle captured world snapshot on turn manager after GameMode capture failed (%d territories)"),
               NewSnapshotCount);
      }
      GI->CachedWorldMapTerritories = MoveTemp(TerritorySnapshots);
    } else {
      TerritorySnapshots.Reset();
    }

    TravelState.CachedTerritories = MoveTemp(TravelSnapshots);

    if (UWorld *WorldContext = World) {
      if (ASkaldGameMode *GameMode = WorldContext->GetAuthGameMode<ASkaldGameMode>()) {
        const TArray<FS_PlayerData> &PlayerSnapshots =
            GameMode->GetPlayerDataSnapshots();
        if (PlayerSnapshots.Num() > 0) {
          TravelState.PlayerSnapshots = PlayerSnapshots;
        }
      }
    }

    if (TravelState.PlayerSnapshots.Num() == 0 && GS) {
      TravelState.PlayerSnapshots.Reserve(GS->PlayerArray.Num());
      for (APlayerState *BasePS : GS->PlayerArray) {
        if (ASkaldPlayerState *SkaldPS = Cast<ASkaldPlayerState>(BasePS)) {
          FS_PlayerData Snapshot;
          Snapshot.PlayerID = SkaldPS->GetPlayerId();
          Snapshot.PlayerName = SkaldPS->GetResolvedPlayerName(TEXT("TravelState"));
          Snapshot.IsAI = SkaldPS->bIsAI;
          Snapshot.IsHuman = !SkaldPS->bIsAI;
          Snapshot.IsEliminated = SkaldPS->IsEliminated;
          Snapshot.Resources = SkaldPS->Resources;
          Snapshot.Faction = SkaldPS->Faction;
          TravelState.PlayerSnapshots.Add(MoveTemp(Snapshot));
        }
      }
    }

    if (GS && TravelState.PlayerSnapshots.Num() > 0) {
      TMap<int32, int32> SnapshotIndexById;
      SnapshotIndexById.Reserve(TravelState.PlayerSnapshots.Num());
      for (int32 Index = 0; Index < TravelState.PlayerSnapshots.Num(); ++Index) {
        const int32 PlayerId = TravelState.PlayerSnapshots[Index].PlayerID;
        if (PlayerId > 0 && !SnapshotIndexById.Contains(PlayerId)) {
          SnapshotIndexById.Add(PlayerId, Index);
        }
      }

      TSet<int32> ValidPlayerIds;
      for (APlayerState *BasePS : GS->PlayerArray) {
        ASkaldPlayerState *SkaldPS = Cast<ASkaldPlayerState>(BasePS);
        if (!SkaldPS) {
          continue;
        }

        const int32 PlayerId = SkaldPS->GetPlayerId();
        if (PlayerId <= 0) {
          continue;
        }

        ValidPlayerIds.Add(PlayerId);

        FS_PlayerData *SnapshotPtr = nullptr;
        if (int32 *ExistingIndex = SnapshotIndexById.Find(PlayerId)) {
          SnapshotPtr = &TravelState.PlayerSnapshots[*ExistingIndex];
        } else {
          const int32 NewIndex = TravelState.PlayerSnapshots.AddDefaulted();
          SnapshotPtr = &TravelState.PlayerSnapshots[NewIndex];
          SnapshotPtr->PlayerID = PlayerId;
          SnapshotIndexById.Add(PlayerId, NewIndex);
        }

        SnapshotPtr->PlayerName =
            SkaldPS->GetResolvedPlayerName(TEXT("TravelState"));
        SnapshotPtr->IsAI = SkaldPS->bIsAI;
        SnapshotPtr->IsHuman = !SkaldPS->bIsAI;
        SnapshotPtr->IsEliminated = SkaldPS->IsEliminated;
        SnapshotPtr->Resources = SkaldPS->Resources;
        SnapshotPtr->Faction = SkaldPS->Faction;
      }

      TravelState.PlayerSnapshots.RemoveAll([&](const FS_PlayerData &Snapshot) {
        return Snapshot.PlayerID <= 0 || !ValidPlayerIds.Contains(Snapshot.PlayerID);
      });
    }

    if (GI) {
      GI->SetPendingTravelSnapshot(TravelState.CachedTerritories);
    }

    if (TravelState.CachedTerritories.Num() == 0) {
      UE_LOG(LogSkald, Warning,
             TEXT("TriggerGridBattle deferred: territory snapshot unavailable; retrying before travel."));

      if (GI) {
        GI->SetTravelPending(false);
        GI->SetBattleMapActive(false);
        GI->bResumeTurns = false;
      }

      if (World && !World->GetTimerManager().IsTimerActive(PendingBattleTravelRetryHandle)) {
        FTimerDelegate RetryDelegate = FTimerDelegate::CreateUObject(
            this, &ATurnManager::RetryPendingBattleTravel);
        constexpr float RetryDelaySeconds = 0.1f;
        World->GetTimerManager().SetTimer(PendingBattleTravelRetryHandle, RetryDelegate,
                                          RetryDelaySeconds, false);
      }

      return;
    }

    if (World) {
      World->GetTimerManager().ClearTimer(PendingBattleTravelRetryHandle);
    }

    FS_BattlePayload PendingPayload = SeededBattle;
    PendingPayload.FromTerritoryID = SeededBattle.FromTerritoryID;
    PendingPayload.TargetTerritoryID = SeededBattle.TargetTerritoryID;

    auto FindTerritory = [&](int32 TerritoryId) -> ATerritory * {
      if (!WorldMap) {
        return nullptr;
      }
      for (ATerritory *Territory : WorldMap->Territories) {
        if (Territory && Territory->TerritoryID == TerritoryId) {
          return Territory;
        }
      }
      return nullptr;
    };

    auto ResolveParticipant = [&](int32 TerritoryId,
                                   ASkaldPlayerState *ExistingPS,
                                   int32 &OutPlayerID, FString &OutName,
                                   ESkaldFaction &OutFaction, bool &bOutIsAI)
        -> ASkaldPlayerState * {
      ASkaldPlayerState *Resolved = ExistingPS;
      if (!Resolved) {
        if (ATerritory *Territory = FindTerritory(TerritoryId)) {
          Resolved = Territory->OwningPlayer;
        }
      }
      if (!Resolved && GS && OutPlayerID > 0) {
        Resolved = GS->GetPlayerById(OutPlayerID);
      }
      if (Resolved) {
        OutPlayerID = Resolved->GetPlayerId();
        OutName = Resolved->GetResolvedPlayerName(TEXT("TriggerGridBattle"));
        OutFaction = Resolved->Faction;
        bOutIsAI = Resolved->bIsAI;
      }
      return Resolved;
    };

    ResolveParticipant(PendingPayload.FromTerritoryID, nullptr,
                      PendingPayload.AttackerPlayerID,
                      PendingPayload.AttackerDisplayName,
                      PendingPayload.AttackerFaction,
                      PendingPayload.bAttackerIsAI);
    ResolveParticipant(PendingPayload.TargetTerritoryID, nullptr,
                      PendingPayload.DefenderPlayerID,
                      PendingPayload.DefenderDisplayName,
                      PendingPayload.DefenderFaction,
                      PendingPayload.bDefenderIsAI);

    ATerritory *DefTerritory = FindTerritory(PendingPayload.TargetTerritoryID);
    if (PendingPayload.DefenderArmyCount <= 0) {
      if (DefTerritory) {
        PendingPayload.DefenderArmyCount = DefTerritory->ArmyUnits;
      }
    }
    if (PendingPayload.DefenderTerritoryName.IsEmpty() && DefTerritory) {
      PendingPayload.DefenderTerritoryName = DefTerritory->TerritoryName;
    }
    if (PendingPayload.DefenderArmyCount <= 0) {
      PendingPayload.DefenderArmyCount = PendingPayload.ArmyCountSent;
    }

    PendingBattle = PendingPayload;
    bool bStreamingBattle = false;
    if (GI) {
      GI->SetTravelState(TravelState);
      GI->PendingBattle = PendingPayload;

      if (USkaldBattleLevelManager *BattleLevelManager =
              GI->GetBattleLevelManager()) {
        if (bShouldStreamSelectedMap) {
          bStreamingBattle = BattleLevelManager->RequestBattleLevel(
              World, SelectedBattleMap, PendingPayload);
        }
      }

      if (bShouldStreamSelectedMap) {
        if (bStreamingBattle) {
          if (World->GetNetMode() != NM_Standalone) {
            MulticastStreamBattleLevel(SelectedBattleMap.ToSoftObjectPath(),
                                       TravelState, PendingPayload);
          }
        } else {
          GI->SetTravelPending(true);
        }
      } else {
        GI->SetTravelPending(true);
      }
      GI->SetBattleMapActive(true);

      if (World->GetNetMode() != NM_Standalone) {
        MulticastSetBattleMapActive(true);
      }
    }

    UE_LOG(LogSkald, Log,
           TEXT("TriggerGridBattle: AttackerID=%d Name=%s Budget=%d DefenderID=%d Name=%s Budget=%d HumanOwned=%d CachedTerritories=%d"),
           PendingPayload.AttackerPlayerID, *PendingPayload.AttackerDisplayName,
           PendingPayload.ArmyCountSent, PendingPayload.DefenderPlayerID,
           *PendingPayload.DefenderDisplayName,
           PendingPayload.DefenderArmyCount,
           TravelState.HumanOwnedTerritories.Num(),
           TravelState.CachedTerritories.Num());

    if (!bShouldStreamSelectedMap || !bStreamingBattle) {
      if (World->GetNetMode() != NM_Standalone) {
        MulticastPrepareBattleTravel(TravelState, PendingPayload);
      }
      if (IsRunningDedicatedServer() ||
          World->GetNetMode() != NM_Standalone) {
        FString ListenMap = MapToLoad;
        if (!ListenMap.Contains(TEXT("?"))) {
          ListenMap.Append(TEXT("?listen"));
        }
        World->ServerTravel(ListenMap);
      } else {
        const FName LevelName = FName(*MapToLoad);
        UGameplayStatics::OpenLevel(World, LevelName, /*bAbsolute=*/true);
      }
    }
  }
}

void ATurnManager::NotifyPlayerReadyForBattle(int32 PlayerID) {
  const bool bHasPendingBattle =
      PendingBattlePreparation.FromTerritoryID != 0 ||
      PendingBattlePreparation.TargetTerritoryID != 0;
  if (!bHasPendingBattle) {
    UE_LOG(LogSkald, Verbose,
           TEXT("NotifyPlayerReadyForBattle ignored: no pending battle."));
    return;
  }

  if (PendingBattleReadyState.AttackerPlayerID == PlayerID) {
    PendingBattleReadyState.bAttackerReady = true;
  }
  if (PendingBattleReadyState.DefenderPlayerID == PlayerID) {
    PendingBattleReadyState.bDefenderReady = true;
  }

  TryLaunchPreparedBattle();
}

void ATurnManager::BroadcastPrepareForBattlePrompt(
    const FS_BattlePayload &Battle) {
  const bool bNeedsAttackerConfirmation =
      !PendingBattleReadyState.bAttackerReady &&
      PendingBattleReadyState.AttackerPlayerID != INDEX_NONE;
  const bool bNeedsDefenderConfirmation =
      !PendingBattleReadyState.bDefenderReady &&
      PendingBattleReadyState.DefenderPlayerID != INDEX_NONE;

  for (ASkaldPlayerController *Controller : GetControllers()) {
    if (Controller) {
      if (Controller->HasAuthority() && Controller->IsLocalController()) {
        Controller->HidePrepareForBattleDialogLocal();
      } else {
        Controller->ClientHidePrepareForBattle();
      }
    }
  }

  if (!bNeedsAttackerConfirmation && !bNeedsDefenderConfirmation) {
    return;
  }

  for (ASkaldPlayerController *Controller : GetControllers()) {
    if (!Controller) {
      continue;
    }
    ASkaldPlayerState *PS = Controller->GetPlayerState<ASkaldPlayerState>();
    const int32 PlayerID = PS ? PS->GetPlayerId() : -1;
    const bool bIsAttacker =
        bNeedsAttackerConfirmation &&
        PlayerID == PendingBattleReadyState.AttackerPlayerID;
    const bool bIsDefender =
        bNeedsDefenderConfirmation &&
        PlayerID == PendingBattleReadyState.DefenderPlayerID;
    if (bIsAttacker || bIsDefender) {
      if (Controller->HasAuthority() && Controller->IsLocalController()) {
        Controller->ShowPrepareForBattleDialogLocal(Battle);
      } else {
        Controller->ClientShowPrepareForBattle(Battle);
      }
    }
  }
}

bool ATurnManager::HasPendingBattlePreparation() const {
  return PendingBattlePreparation.FromTerritoryID != 0 ||
         PendingBattlePreparation.TargetTerritoryID != 0;
}

void ATurnManager::EnsureBattleParticipantsRegistered() {
  if (!HasPendingBattlePreparation()) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator();
       It; ++It) {
    ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(*It);
    if (!PC || Controllers.Contains(PC)) {
      continue;
    }

    ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>();
    if (!PS) {
      continue;
    }

    const int32 PlayerID = PS->GetPlayerId();
    const bool bNeedsAttacker =
        !PendingBattleReadyState.bAttackerReady &&
        PendingBattleReadyState.AttackerPlayerID == PlayerID;
    const bool bNeedsDefender =
        !PendingBattleReadyState.bDefenderReady &&
        PendingBattleReadyState.DefenderPlayerID == PlayerID;

    if (bNeedsAttacker || bNeedsDefender) {
      RegisterControllerInternal(PC, /*bSuppressPreparePrompt=*/true);
    }
  }
}

void ATurnManager::MaybePromptPendingBattleParticipant(
    ASkaldPlayerController *Controller) {
  if (!HasPendingBattlePreparation() || !Controller) {
    return;
  }

  ASkaldPlayerState *PS = Controller->GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    return;
  }

  const int32 PlayerID = PS->GetPlayerId();
  const bool bNeedsAttacker =
      !PendingBattleReadyState.bAttackerReady &&
      PendingBattleReadyState.AttackerPlayerID == PlayerID;
  const bool bNeedsDefender =
      !PendingBattleReadyState.bDefenderReady &&
      PendingBattleReadyState.DefenderPlayerID == PlayerID;

  if (bNeedsAttacker || bNeedsDefender) {
    BroadcastPrepareForBattlePrompt(PendingBattlePreparation);
  }
}

void ATurnManager::TryLaunchPreparedBattle() {
  const bool bHasPendingBattle =
      PendingBattlePreparation.FromTerritoryID != 0 ||
      PendingBattlePreparation.TargetTerritoryID != 0;
  if (!bHasPendingBattle) {
    return;
  }

  const bool bAttackerReady = PendingBattleReadyState.bAttackerReady ||
                              PendingBattleReadyState.AttackerPlayerID == INDEX_NONE;
  const bool bDefenderReady = PendingBattleReadyState.bDefenderReady ||
                              PendingBattleReadyState.DefenderPlayerID == INDEX_NONE;

  if (!bAttackerReady || !bDefenderReady) {
    return;
  }

  for (ASkaldPlayerController *Controller : GetControllers()) {
    if (Controller) {
      if (Controller->HasAuthority() && Controller->IsLocalController()) {
        Controller->HidePrepareForBattleDialogLocal();
      } else {
        Controller->ClientHidePrepareForBattle();
      }
    }
  }

  FS_BattlePayload BattleToLaunch = PendingBattlePreparation;
  PendingBattlePreparation = FS_BattlePayload();
  PendingBattleReadyState = FPendingBattleReadyState();

  TriggerGridBattle(BattleToLaunch);
}

void ATurnManager::RetryPendingBattleTravel() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(PendingBattleTravelRetryHandle);
  }

  const FS_BattlePayload PendingPayload = DeferredPendingBattle;

  if (PendingPayload.FromTerritoryID <= 0 ||
      PendingPayload.TargetTerritoryID <= 0) {
    return;
  }

  TriggerGridBattle(PendingPayload);
}

void ATurnManager::MulticastStreamBattleLevel_Implementation(
    const FSoftObjectPath &BattleLevelPath, const FSkaldTravelState &TravelState,
    const FS_BattlePayload &BattlePayload) {
  if (HasAuthority()) {
    return;
  }

  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI) {
    return;
  }

  GI->SetTravelState(TravelState);
  GI->PendingBattle = BattlePayload;
  GI->SetBattleMapActive(true);

  if (USkaldBattleLevelManager *BattleLevelManager =
          GI->GetBattleLevelManager()) {
    if (UWorld *World = GetWorld()) {
      TSoftObjectPtr<UWorld> LevelToStream;
      if (BattleLevelPath.IsValid()) {
        LevelToStream = TSoftObjectPtr<UWorld>(BattleLevelPath);
      }

      const bool bRequested = BattleLevelManager->RequestBattleLevel(
          World, LevelToStream, BattlePayload);
      if (!bRequested) {
        GI->SetTravelPending(true);
        UE_LOG(LogSkald, Warning,
               TEXT("MulticastStreamBattleLevel failed to stream %s on client"),
               *BattleLevelPath.ToString());
      }
    }
  }
}

void ATurnManager::MulticastPrepareBattleTravel_Implementation(
    const FSkaldTravelState &TravelState,
    const FS_BattlePayload &BattlePayload) {
  if (HasAuthority()) {
    return;
  }

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->SetTravelState(TravelState);
    GI->PendingBattle = BattlePayload;
    GI->SetBattleMapActive(true);
    GI->SetPendingReturnMap(TravelState.ReturnMap);
    GI->SetTravelPending(true);
  }
}

void ATurnManager::MulticastSetBattleMapActive_Implementation(bool bInBattleMap) {
  if (HasAuthority()) {
    return;
  }

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->SetBattleMapActive(bInBattleMap);
  }
}

void ATurnManager::ResolveGridBattleResult_Implementation() {
  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI) {
    return;
  }

  GI->SetBattleMapActive(false);
  if (HasAuthority()) {
    MulticastSetBattleMapActive(false);
  }
  if (USkaldBattleLevelManager *BattleLevelManager =
          GI->GetBattleLevelManager()) {
    BattleLevelManager->ReleaseBattleLevel();
  }

  // Always mirror the pending payload locally for reference.
  PendingBattle = GI->PendingBattle;

  const bool bHasResolution = CapturePendingBattleResolution(GI);

  if (!bHasResolution) {
    GI->SetTravelPending(false);
    return;
  }

  ASkaldGameMode *GameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();

  auto QueueRetry = [&]() {
    GI->SetTravelPending(true);

    UWorld *World = GetWorld();
    if (!World) {
      UE_LOG(LogSkald, Verbose,
             TEXT("ResolveGridBattleResult: World unavailable; awaiting travel completion before retry."));
      return;
    }

    FTimerManager &TimerManager = World->GetTimerManager();
    if (!TimerManager.IsTimerActive(PendingBattleResolutionRetryHandle)) {
      FTimerDelegate RetryDelegate = FTimerDelegate::CreateUObject(
          this, &ATurnManager::ResolveGridBattleResult);
      constexpr float RetryDelaySeconds = 0.05f;
      TimerManager.SetTimer(PendingBattleResolutionRetryHandle, RetryDelegate,
                            RetryDelaySeconds, false);
    }
  };

  if (GameMode && GameMode->IsA(ASkald_BattleGameMode::StaticClass())) {
    // Still on the battle map; wait for travel to finish before updating the
    // overworld. The pending resolution will be applied once the world map
    // has been rebuilt.
    QueueRetry();
    return;
  }

  if (!GI->bPendingBattleResolution || !GI->PendingBattleResolution.bValid) {
    GI->SetTravelPending(false);
    return;
  }
  if (GameMode && !GameMode->IsWorldInitialized()) {
    UE_LOG(LogSkald, Verbose,
           TEXT("ResolveGridBattleResult: World not yet initialised; retrying after snapshot restoration."));

    QueueRetry();
    return;
  }

  auto DeferResolution = [&](const TCHAR *Reason) {
    UE_LOG(LogSkald, Verbose,
           TEXT("ResolveGridBattleResult: %s; retrying."), Reason);
    QueueRetry();
  };

  AWorldMap *WorldMap = ResolveWorldMap();

  if (!WorldMap) {
    DeferResolution(TEXT("World map unavailable"));
    return;
  }

  if (WorldMap->Territories.Num() == 0) {
    DeferResolution(TEXT("World map territories not yet generated"));
    return;
  }

  FS_BattlePayload Battle = GI->PendingBattle;
  int32 FromTerritoryID = Battle.FromTerritoryID;
  int32 TargetTerritoryID = Battle.TargetTerritoryID;

  auto ResolveFromSnapshots = [](const TArray<FS_Territory> &Snapshots,
                                 TFunctionRef<bool(const FS_Territory &)> Predicate)
      -> int32 {
    for (const FS_Territory &Snapshot : Snapshots) {
      if (Predicate(Snapshot)) {
        return Snapshot.TerritoryID;
      }
    }
    return 0;
  };

  auto TryResolveMissingIds = [&](FS_BattlePayload &Payload) {
    if (!GI) {
      return;
    }

    const FSkaldTravelState &TravelState = GI->GetTravelState();
    if (FromTerritoryID <= 0 && TravelState.AttackerTerritory > 0) {
      FromTerritoryID = TravelState.AttackerTerritory;
    }
    if (TargetTerritoryID <= 0 && TravelState.DefenderTerritory > 0) {
      TargetTerritoryID = TravelState.DefenderTerritory;
    }

    auto ResolveUsingSnapshots = [&](const TArray<FS_Territory> &Snapshots,
                                     const TCHAR *SourceLabel) {
      if (Snapshots.Num() == 0) {
        UE_LOG(LogSkald, Verbose,
               TEXT("ResolveGridBattleResult: %s snapshot empty"), SourceLabel);
        return;
      }

      if (TargetTerritoryID <= 0 && !Payload.DefenderTerritoryName.IsEmpty()) {
        const FString TargetName = Payload.DefenderTerritoryName;
        const int32 ResolvedTarget = ResolveFromSnapshots(
            Snapshots, [&TargetName](const FS_Territory &Territory) {
              return Territory.TerritoryName.Equals(TargetName,
                                                    ESearchCase::IgnoreCase);
            });
        if (ResolvedTarget > 0) {
          TargetTerritoryID = ResolvedTarget;
          UE_LOG(LogSkald, Log,
                 TEXT("ResolveGridBattleResult: Resolved defender '%s' -> %d using %s"),
                 *TargetName, TargetTerritoryID, SourceLabel);
        }
      }

      if (FromTerritoryID <= 0 && Payload.AttackerPlayerID > 0) {
        const int32 Candidate = ResolveFromSnapshots(
            Snapshots, [&](const FS_Territory &Territory) {
              if (Territory.OwnerPlayerID != Payload.AttackerPlayerID) {
                return false;
              }
              if (TargetTerritoryID > 0) {
                return Territory.AdjacentIDs.Contains(TargetTerritoryID);
              }
              return true;
            });
        if (Candidate > 0) {
          FromTerritoryID = Candidate;
          UE_LOG(LogSkald, Log,
                 TEXT("ResolveGridBattleResult: Resolved attacker territory for PlayerID=%d -> %d using %s"),
                 Payload.AttackerPlayerID, FromTerritoryID, SourceLabel);
        }
      }
    };

    ResolveUsingSnapshots(TravelState.CachedTerritories, TEXT("TravelState"));
    if (FromTerritoryID <= 0 || TargetTerritoryID <= 0) {
      ResolveUsingSnapshots(GI->CachedWorldMapTerritories,
                            TEXT("CachedWorldMap"));
    }
    if (FromTerritoryID <= 0 || TargetTerritoryID <= 0) {
      ResolveUsingSnapshots(GI->GetPendingTravelSnapshot(),
                            TEXT("PendingSnapshot"));
    }
  };

  if (FromTerritoryID <= 0 || TargetTerritoryID <= 0) {
    TryResolveMissingIds(Battle);
    UE_LOG(LogSkald, Verbose,
           TEXT("ResolveGridBattleResult: After TryResolveMissingIds -> From=%d Target=%d"),
           FromTerritoryID, TargetTerritoryID);
  }

  if ((FromTerritoryID <= 0 || TargetTerritoryID <= 0) &&
      (PendingBattle.FromTerritoryID > 0 || PendingBattle.TargetTerritoryID > 0)) {
    if (FromTerritoryID <= 0 && PendingBattle.FromTerritoryID > 0) {
      FromTerritoryID = PendingBattle.FromTerritoryID;
    }
    if (TargetTerritoryID <= 0 && PendingBattle.TargetTerritoryID > 0) {
      TargetTerritoryID = PendingBattle.TargetTerritoryID;
    }
  }

  if (FromTerritoryID <= 0 || TargetTerritoryID <= 0) {
    UE_LOG(LogSkald, Error,
           TEXT("ResolveGridBattleResult: Unable to resolve territory ids (From=%d Target=%d)."),
           FromTerritoryID, TargetTerritoryID);
    UE_LOG(LogSkald, Error,
           TEXT(
               "ResolveGridBattleResult: TravelStateValid=%d CachedTravel=%d PendingSnapshot=%d PendingBattle From=%d Target=%d"),
           GI && GI->GetTravelState().bValid ? 1 : 0,
           GI ? GI->GetTravelState().CachedTerritories.Num() : 0,
           GI ? GI->GetPendingTravelSnapshot().Num() : 0,
           PendingBattle.FromTerritoryID, PendingBattle.TargetTerritoryID);
    GI->bPendingBattleResolution = false;
    GI->PendingBattleResolution = FGridBattleResolution();
    GI->SetTravelPending(false);
    return;
  }

  if (Battle.FromTerritoryID != FromTerritoryID ||
      Battle.TargetTerritoryID != TargetTerritoryID) {
    UE_LOG(LogSkald, Warning,
           TEXT("ResolveGridBattleResult: Reconstructed territory ids From=%d Target=%d (Original From=%d Target=%d)."),
           FromTerritoryID, TargetTerritoryID, Battle.FromTerritoryID,
           Battle.TargetTerritoryID);
    Battle.FromTerritoryID = FromTerritoryID;
    Battle.TargetTerritoryID = TargetTerritoryID;
    GI->PendingBattle = Battle;
    PendingBattle = Battle;
  }

  ATerritory *Source = WorldMap->GetTerritoryById(FromTerritoryID);
  ATerritory *Target = WorldMap->GetTerritoryById(TargetTerritoryID);
  if (!Source || !Target) {
    DeferResolution(TEXT("Battle territories pending restoration"));
    return;
  }

  auto ResolvePlayerById = [&](int32 PlayerId) -> ASkaldPlayerState * {
    if (PlayerId <= 0) {
      return nullptr;
    }
    if (ASkaldGameState *GS = GetWorld()->GetGameState<ASkaldGameState>()) {
      return GS->GetPlayerById(PlayerId);
    }
    return nullptr;
  };

  ASkaldPlayerState *AttackerPS = ResolvePlayerById(Battle.AttackerPlayerID);
  ASkaldPlayerState *DefenderPS = ResolvePlayerById(Battle.DefenderPlayerID);

  const bool bSourceOwnedByAttacker =
      (Source && AttackerPS && Source->OwningPlayer == AttackerPS);
  const bool bTargetOwnedByDefender =
      (Target && DefenderPS && Target->OwningPlayer == DefenderPS);
  const bool bSourceOwnedByDefender =
      (Source && DefenderPS && Source->OwningPlayer == DefenderPS);
  const bool bTargetOwnedByAttacker =
      (Target && AttackerPS && Target->OwningPlayer == AttackerPS);

  if (!bSourceOwnedByAttacker && bTargetOwnedByAttacker &&
      bSourceOwnedByDefender && !bTargetOwnedByDefender) {
    UE_LOG(LogSkald, Warning,
           TEXT("ResolveGridBattleResult detected inverted territories; swapping Source/Target (From=%d Target=%d)."),
           FromTerritoryID, TargetTerritoryID);

    Swap(Source, Target);
    Swap(FromTerritoryID, TargetTerritoryID);
    Battle.FromTerritoryID = FromTerritoryID;
    Battle.TargetTerritoryID = TargetTerritoryID;
    PendingBattle = Battle;
    if (GI) {
      GI->PendingBattle = Battle;
    }
  }

  GetWorld()->GetTimerManager().ClearTimer(
      PendingBattleResolutionRetryHandle);

  FGridBattleResolution Resolution = GI->PendingBattleResolution;

  const int32 InitialSourceArmy = Source->ArmyUnits;
  const int32 InitialTargetArmy = Target->ArmyUnits;

  const int32 AttackerBudget =
      Battle.ArmyCountSent > 0 ? Battle.ArmyCountSent : InitialSourceArmy;
  int32 AttackerCommitted = Resolution.AttackerCommittedArmyCost;
  if (AttackerCommitted <= 0) {
    AttackerCommitted = AttackerBudget;
  }
  AttackerCommitted = FMath::Clamp(AttackerCommitted, 0, FMath::Min(AttackerBudget, InitialSourceArmy));

  const int32 DefenderBudget =
      Battle.DefenderArmyCount > 0 ? Battle.DefenderArmyCount : InitialTargetArmy;
  int32 DefenderCommitted = Resolution.DefenderCommittedArmyCost;
  if (DefenderCommitted <= 0) {
    DefenderCommitted = DefenderBudget;
  }
  DefenderCommitted = FMath::Clamp(DefenderCommitted, 0, FMath::Min(DefenderBudget, InitialTargetArmy));
  const int32 DefenderUnspent = FMath::Max(0, DefenderBudget - DefenderCommitted);

  Source->ArmyUnits = FMath::Max(0, InitialSourceArmy - AttackerCommitted);

  if (Resolution.AttackerSurvivorArmyCost > 0 &&
      Resolution.DefenderSurvivorArmyCost <= 0) {
    Target->OwningPlayer = Source->OwningPlayer;
    Target->ArmyUnits = Resolution.AttackerSurvivorArmyCost;
  } else {
    int32 DefenderResult = FMath::Max(0, Resolution.DefenderSurvivorArmyCost);
    Target->ArmyUnits = DefenderResult + DefenderUnspent;
  }

  Resolution.AttackerCommittedArmyCost = AttackerCommitted;
  Resolution.DefenderCommittedArmyCost = DefenderCommitted;
  Resolution.SourceArmyRemaining = Source->ArmyUnits;
  Resolution.TargetArmyRemaining = Target->ArmyUnits;

  Resolution.AttackerCasualties =
      FMath::Max(0, AttackerCommitted - Resolution.AttackerSurvivorArmyCost);
  Resolution.DefenderCasualties =
      FMath::Max(0, DefenderCommitted - Resolution.DefenderSurvivorArmyCost);

  Source->RefreshAppearance();
  Target->RefreshAppearance();

  bool bUpdatedSnapshot = false;
  if (GI) {
    bUpdatedSnapshot = GI->CacheWorldMapSnapshot(GetWorld());
    if (!bUpdatedSnapshot && GI->CachedWorldMapTerritories.Num() > 0) {
      bUpdatedSnapshot = true;
    }
  }

  if (bUpdatedSnapshot && GI) {
    FSkaldTravelState UpdatedTravelState = GI->GetTravelState();
    if (UpdatedTravelState.bValid) {
      UpdatedTravelState.CachedTerritories = GI->CachedWorldMapTerritories;
      GI->SetTravelState(UpdatedTravelState);
    }
  }

  if (GI) {
    GI->ClearPendingTravelSnapshot();
  }

  GI->PendingBattle = FS_BattlePayload();
  PendingBattle = FS_BattlePayload();
  GI->ClearPendingReturnMap();

  // Ensure no stale retry timers trigger another battle after resolution
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(PendingBattleTravelRetryHandle);
  }
  DeferredPendingBattle = FS_BattlePayload();

  const int32 WinningPlayerID = Resolution.WinningPlayerID;
  const int32 NewOwnerPlayerID = Resolution.NewOwnerPlayerID;
  const int32 AttackerCasualties = Resolution.AttackerCasualties;
  const int32 DefenderCasualties = Resolution.DefenderCasualties;

  GI->bPendingBattleResolution = false;
  GI->PendingBattleResolution = FGridBattleResolution();
  GI->SetTravelPending(false);

  // Resume the saved turn sequence now that the battle has been resolved.
  TryResumeSavedTurnState(GI);

  if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
    GM->CheckVictoryConditions();
  }

  ClientBattleResolved(WinningPlayerID, AttackerCasualties, DefenderCasualties,
                       Source->TerritoryID, Target->TerritoryID,
                       NewOwnerPlayerID, Source->ArmyUnits, Target->ArmyUnits);
}

void ATurnManager::ClientBattleResolved_Implementation(
    int32 WinningPlayerID, int32 AttackerCasualties, int32 DefenderCasualties,
    int32 FromTerritoryID, int32 TargetTerritoryID, int32 NewOwnerPlayerID,
    int32 SourceArmy, int32 TargetArmy) {
  if (AWorldMap *WorldMapForClient = ResolveWorldMap()) {
    ATerritory *Source = WorldMapForClient->GetTerritoryById(FromTerritoryID);
    ATerritory *Target = WorldMapForClient->GetTerritoryById(TargetTerritoryID);
    if (Source) {
      Source->ArmyUnits = SourceArmy;
      Source->RefreshAppearance();
    }
    if (Target) {
      ASkaldPlayerState *NewOwner = nullptr;
      if (ASkaldGameState *GS = GetWorld()->GetGameState<ASkaldGameState>()) {
        NewOwner = GS->GetPlayerById(NewOwnerPlayerID);
      }
      Target->OwningPlayer = NewOwner;
      Target->ArmyUnits = TargetArmy;
      Target->RefreshAppearance();
    }
  }

  for (FConstPlayerControllerIterator It =
           GetWorld()->GetPlayerControllerIterator();
       It; ++It) {
    if (ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(It->Get())) {
      if (USkaldMainHUDWidget *HUD = PC->GetHUDWidget()) {
        FString WinnerName = TEXT("Unknown");
        if (ASkaldGameState *GSLocal =
                GetWorld()->GetGameState<ASkaldGameState>()) {
          if (ASkaldPlayerState *WinnerPS =
                  GSLocal->GetPlayerById(WinningPlayerID)) {
            WinnerName =
                GetResolvedPlayerName(WinnerPS, TEXT("ClientBattleResolved"));
          }
        }
        const FString Msg = FString::Printf(
            TEXT("%s won: A-%d D-%d casualties"), *WinnerName,
            AttackerCasualties, DefenderCasualties);
        HUD->UpdateInitiativeText(Msg);
      }
      PC->HandleWorldStateChanged();
    }
  }

  OnWorldStateChanged.Broadcast();
}

bool ATurnManager::CaptureWorldSnapshot(
    TArray<FS_Territory> &OutSnapshot,
    TSet<int32> *OutHumanOwnedTerritories) {
  OutSnapshot.Reset();
  if (OutHumanOwnedTerritories) {
    OutHumanOwnedTerritories->Reset();
  }

  AWorldMap *WorldMapForSnapshot = CachedWorldMap;
  if (!IsValid(WorldMapForSnapshot)) {
    WorldMapForSnapshot = ResolveWorldMap();
  }

  if (!IsValid(WorldMapForSnapshot) ||
      WorldMapForSnapshot->Territories.Num() == 0) {
    return false;
  }

  UWorld *World = GetWorld();
  ASkaldGameState *GameState =
      World ? World->GetGameState<ASkaldGameState>() : nullptr;

  OutSnapshot.Reserve(WorldMapForSnapshot->Territories.Num());

  for (ATerritory *Territory : WorldMapForSnapshot->Territories) {
    if (!Territory) {
      continue;
    }

    FS_Territory Snapshot;
    Snapshot.TerritoryID = Territory->TerritoryID;
    Snapshot.TerritoryName = Territory->TerritoryName;
    ASkaldPlayerState *OwnerPS = Territory->OwningPlayer;
    Snapshot.OwnerPlayerID = OwnerPS ? OwnerPS->GetPlayerId() : 0;
    Snapshot.IsCapital = Territory->bIsCapital;
    Snapshot.CapitalOwner = Snapshot.OwnerPlayerID;
    Snapshot.ArmyUnits = Territory->ArmyUnits;
    Snapshot.ContinentID = Territory->ContinentID;
    Snapshot.Location = Territory->GetActorLocation();
    Snapshot.HasTreasure = Territory->bHasTreasure;
    Snapshot.TreasureAttachedUnitID =
        ReadIntProperty(Territory, TEXT("TreasureAttachedUnitID"));
    Snapshot.FortificationLevel =
        ReadIntProperty(Territory, TEXT("FortificationLevel"));
    Snapshot.Moat = ReadBoolProperty(Territory, TEXT("Moat"));
    Snapshot.WallHealth = ReadIntProperty(Territory, TEXT("WallHealth"));
    Snapshot.BuiltSiegeID = Territory->BuiltSiegeID;
    Snapshot.ConqueredTurn =
        ReadIntProperty(Territory, TEXT("ConqueredTurn"));
    Snapshot.IsNeutralSpawn =
        ReadBoolProperty(Territory, TEXT("IsNeutralSpawn"));
    Snapshot.AdjacentIDs.Reset();
    for (ATerritory *Adjacent : Territory->AdjacentTerritories) {
      if (Adjacent) {
        Snapshot.AdjacentIDs.Add(Adjacent->TerritoryID);
      }
    }

    if (!OwnerPS && GameState && Snapshot.OwnerPlayerID > 0) {
      OwnerPS = GameState->GetPlayerById(Snapshot.OwnerPlayerID);
    }
    if (OutHumanOwnedTerritories && OwnerPS && !OwnerPS->bIsAI) {
      OutHumanOwnedTerritories->Add(Snapshot.TerritoryID);
    }

    OutSnapshot.Add(MoveTemp(Snapshot));
  }

  return OutSnapshot.Num() > 0;
}

AWorldMap *ATurnManager::ResolveWorldMap() {
  if (IsValid(CachedWorldMap)) {
    return CachedWorldMap;
  }

  CachedWorldMap = nullptr;

  if (UWorld *World = GetWorld()) {
    if (AActor *Actor =
            UGameplayStatics::GetActorOfClass(World, AWorldMap::StaticClass())) {
      if (AWorldMap *FoundMap = Cast<AWorldMap>(Actor)) {
        if (IsValid(FoundMap)) {
          CachedWorldMap = FoundMap;
        }
      }
    }
  }

  return IsValid(CachedWorldMap) ? CachedWorldMap : nullptr;
}

bool ATurnManager::CapturePendingBattleResolution(
    USkaldGameInstance *GameInstance) {
  if (!GameInstance) {
    return false;
  }

  if (GameInstance->bPendingBattleResolution &&
      GameInstance->PendingBattleResolution.bValid) {
    return true;
  }

  if (!GameInstance->GridBattleManager) {
    return false;
  }

  FGridBattleResolution Resolution;
  Resolution.bValid = true;
  Resolution.AttackerSurvivorArmyCost =
      GameInstance->GridBattleManager->GetAttackerSurvivorCost();
  Resolution.DefenderSurvivorArmyCost =
      GameInstance->GridBattleManager->GetDefenderSurvivorCost();
  Resolution.AttackerCommittedArmyCost =
      GameInstance->GridBattleManager->GetAttackerInitialArmyCost();
  Resolution.DefenderCommittedArmyCost =
      GameInstance->GridBattleManager->GetDefenderInitialArmyCost();
  Resolution.AttackerCasualties =
      Resolution.AttackerCommittedArmyCost -
      Resolution.AttackerSurvivorArmyCost;
  Resolution.DefenderCasualties =
      Resolution.DefenderCommittedArmyCost -
      Resolution.DefenderSurvivorArmyCost;

  const FS_BattlePayload &Battle = GameInstance->PendingBattle;
  UWorld *World = GetWorld();
  ASkaldGameState *GameState =
      World ? World->GetGameState<ASkaldGameState>() : nullptr;
  ESkaldFaction AttackerFaction = ESkaldFaction::None;
  ESkaldFaction DefenderFaction = ESkaldFaction::None;
  if (GameState) {
    if (ASkaldPlayerState *AttackerPS =
            GameState->GetPlayerById(Battle.AttackerPlayerID)) {
      AttackerFaction = AttackerPS->Faction;
    }
    if (ASkaldPlayerState *DefenderPS =
            GameState->GetPlayerById(Battle.DefenderPlayerID)) {
      DefenderFaction = DefenderPS->Faction;
    }
  }

  const bool bAttackerVictory =
      Resolution.AttackerSurvivorArmyCost > 0 &&
      Resolution.DefenderSurvivorArmyCost <= 0;
  const bool bDefenderVictory =
      Resolution.DefenderSurvivorArmyCost > 0 &&
      Resolution.AttackerSurvivorArmyCost <= 0;

  if (bAttackerVictory) {
    Resolution.WinningFaction = AttackerFaction;
    Resolution.WinningPlayerID = Battle.AttackerPlayerID;
    Resolution.NewOwnerPlayerID = Battle.AttackerPlayerID;
  } else if (bDefenderVictory) {
    Resolution.WinningFaction = DefenderFaction;
    Resolution.WinningPlayerID = Battle.DefenderPlayerID;
    Resolution.NewOwnerPlayerID = Battle.DefenderPlayerID;
  } else {
    Resolution.WinningFaction = ESkaldFaction::None;
    Resolution.WinningPlayerID = Battle.DefenderPlayerID;
    Resolution.NewOwnerPlayerID = Battle.DefenderPlayerID;
  }

  GameInstance->PendingBattleResolution = Resolution;
  GameInstance->bPendingBattleResolution = true;
  GameInstance->GridBattleManager = nullptr;

  return true;
}

void ATurnManager::BeginAttackPhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  // Enter the attack phase and notify all listeners so they can swap controls.
  CurrentPhase = ETurnPhase::Attack;

  BroadcastCurrentPhase();
}

void ATurnManager::AdvancePhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  if (CurrentPhase == ETurnPhase::Reinforcement) {
    BeginAttackPhase();
    return;
  }

  switch (CurrentPhase) {
  case ETurnPhase::Attack:
    CurrentPhase = ETurnPhase::Engineering;
    break;
  case ETurnPhase::Engineering:
    CurrentPhase = ETurnPhase::Treasure;
    break;
  case ETurnPhase::Treasure:
    CurrentPhase = ETurnPhase::Movement;
    break;
  case ETurnPhase::Movement:
    CurrentPhase = ETurnPhase::EndTurn;
    break;
  case ETurnPhase::EndTurn:
    CurrentPhase = ETurnPhase::Revolt;
    break;
  default:
    return;
  }

  BroadcastCurrentPhase();
}

void ATurnManager::EndCurrentPhase() {
  const bool bArmyPlacement = CurrentPhase == ETurnPhase::ArmyPlacement;

  if (!bArmyPlacement) {
    if (const UWorld *W = GetWorld()) {
      if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
        if (GI->bTravelPending) {
          return;
        }
      }
    }
  }

  if (bArmyPlacement) {
    bool bBlockAdvance = false;
    if (ASkaldGameState *GS = GetWorld()->GetGameState<ASkaldGameState>()) {
      if (GS->PlayerArray.IsValidIndex(GS->CurrentTurnIndex)) {
        if (ASkaldPlayerState *ActivePS =
                Cast<ASkaldPlayerState>(GS->PlayerArray[GS->CurrentTurnIndex])) {
          if (!ActivePS->bIsAI && ActivePS->DeployableUnits > 0) {
            bBlockAdvance = true;
            UE_LOG(LogSkald, Verbose,
                   TEXT("EndCurrentPhase blocked: Human player %s still has %d units to place."),
                   *ActivePS->GetResolvedPlayerName(TEXT("EndCurrentPhase_ArmyPlacement")),
                   ActivePS->DeployableUnits);
          }
        }
      }
    }

    if (bBlockAdvance) {
      return;
    }

    if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
      GM->AdvanceArmyPlacement();
    }
    return;
  }

  AdvancePhase();
}

void ATurnManager::BroadcastDeployableUnits(ASkaldPlayerState *ForPlayer) {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  if (!ForPlayer) {
    return;
  }
  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr :
       Controllers) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      ASkaldPlayerState *PS = Controller->GetPlayerState<ASkaldPlayerState>();
      if (PS == ForPlayer) {
        if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
          HUD->UpdateDeployableUnits(ForPlayer->DeployableUnits);
        }
      }
    }
  }

  OnWorldStateChanged.Broadcast();
}

void ATurnManager::BroadcastResources(ASkaldPlayerState *ForPlayer) {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  if (!ForPlayer) {
    return;
  }

  if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
    GM->UpdatePlayerResources(ForPlayer);
  }

  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr :
       Controllers) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
        HUD->UpdateResources(ForPlayer->Resources);
      }
    }
  }

  OnWorldStateChanged.Broadcast();
}

bool ATurnManager::BroadcastCurrentPhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending || GI->bIsInBattleMap) {
        return false;
      }
    }
    if (ASkaldGameMode *GameMode = W->GetAuthGameMode<ASkaldGameMode>()) {
      if (!GameMode->IsWorldInitialized() ||
          GameMode->IsAwaitingStrategicInitiative()) {
        return false;
      }
    }
  }

  const FString PhaseString = UEnum::GetValueAsString(CurrentPhase);
  UE_LOG(LogSkald, Log, TEXT("BroadcastCurrentPhase: %s"), *PhaseString);
  if (GEngine) {
    GEngine->AddOnScreenDebugMessage(
        -1, 5.f, FColor::Green,
        FString::Printf(TEXT("Current Phase: %s"), *PhaseString));
  }

  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr :
       Controllers) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
        HUD->UpdatePhaseBanner(CurrentPhase);
      }

      switch (CurrentPhase) {
      case ETurnPhase::Attack:
        Controller->HandleAttackPhase();
        break;
      case ETurnPhase::Engineering:
        Controller->HandleEngineeringPhase();
        break;
      case ETurnPhase::Treasure:
        Controller->HandleTreasurePhase();
        break;
      case ETurnPhase::Movement:
        Controller->HandleMovementPhase();
        break;
      case ETurnPhase::EndTurn:
        Controller->HandleEndTurnPhase();
        break;
      case ETurnPhase::Revolt:
        Controller->HandleRevoltPhase();
        break;
      default:
        break;
      }
    }
  }

  OnWorldStateChanged.Broadcast();

  if (bPhaseBroadcastRetryActive) {
    bPhaseBroadcastRetryActive = false;
    PendingPhaseBroadcast = ETurnPhase::Reinforcement;
    if (UWorld *World = GetWorld()) {
      World->GetTimerManager().ClearTimer(PhaseBroadcastRetryHandle);
    }
  }

  return true;
}

void ATurnManager::QueuePhaseBroadcastRetry(ETurnPhase Phase) {
  if (UWorld *World = GetWorld()) {
    FTimerManager &TimerManager = World->GetTimerManager();
    if (bPhaseBroadcastRetryActive &&
        TimerManager.IsTimerActive(PhaseBroadcastRetryHandle) &&
        PendingPhaseBroadcast == Phase) {
      return;
    }

    PendingPhaseBroadcast = Phase;
    bPhaseBroadcastRetryActive = true;

    constexpr float RetryDelaySeconds = 0.05f;
    FTimerDelegate RetryDelegate = FTimerDelegate::CreateWeakLambda(
        this, [this, Phase]() {
          bPhaseBroadcastRetryActive = false;
          if (CurrentPhase != Phase) {
            return;
          }

          if (!BroadcastCurrentPhase()) {
            QueuePhaseBroadcastRetry(Phase);
          }
        });

    TimerManager.SetTimer(PhaseBroadcastRetryHandle, RetryDelegate,
                          RetryDelaySeconds, false);
  }
}
