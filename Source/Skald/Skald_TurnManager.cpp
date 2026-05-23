#include "Skald_TurnManager.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "GridBattleManager.h"
#include "HAL/PlatformTime.h"
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
#include "Skald_AIController.h"
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
int32 ResolveStableOrPlayerId(const ASkaldPlayerState* PlayerState)
{
  if (!PlayerState)
  {
    return INDEX_NONE;
  }

  const int32 StableId = PlayerState->GetStablePlayerId();
  if (StableId > 0)
  {
    return StableId;
  }

  return PlayerState->GetPlayerId();
}

constexpr float BattleResultReturnDelaySeconds = 5.0f;
constexpr int32 MaxMovementActionsPerMovementPhase = 2;
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
  DOREPLIFETIME(ATurnManager, CapitalMaps);
  DOREPLIFETIME(ATurnManager, BattleMapEntries);
  DOREPLIFETIME(ATurnManager, CurrentPhase);
}

void ATurnManager::OnRep_CurrentPhase(ETurnPhase PreviousPhase) {
  const FString PreviousLabel = UEnum::GetValueAsString(PreviousPhase);
  const FString NewLabel = UEnum::GetValueAsString(CurrentPhase);
  UE_LOG(LogSkald, Verbose, TEXT("OnRep_CurrentPhase: %s -> %s"), *PreviousLabel,
         *NewLabel);

  // Ensure clients refresh their HUDs and local turn/phase visibility from
  // replicated data without waiting for the next server broadcast. This keeps
  // phase buttons correct for the owning controller even when the host isn't
  // the active player.
  if (UWorld *World = GetWorld())
  {
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
      if (ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(It->Get()))
      {
        PC->RefreshTurnDataFromState();
        PC->HandleReplicatedTurnOwnership();
        PC->HandleReplicatedPhaseChange(CurrentPhase);
      }
    }
  }

  OnWorldStateChanged.Broadcast();
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
  if (UWorld *World = GetWorld()) {
    const ENetMode NetMode = World->GetNetMode();
    UE_LOG(LogSkald, Warning,
           TEXT("TurnManager EndPlay (Reason=%d, NetMode=%d). Battle and turn flow should not tear down multiplayer sessions implicitly."),
           static_cast<int32>(EndPlayReason), static_cast<int32>(NetMode));
  }

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
  if (!ResolveBattleReturnMapName(ReturnMapName, ReturnMapSource)) {
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

  if (ReturnMapSource == TEXT("FallbackOverviewMap") && GI) {
    GI->SetPendingReturnMap(ReturnMapName);
  }

  UE_LOG(LogSkald, Verbose,
         TEXT("HandleGridBattleEnded: resolved return map '%s' from %s."),
         *ReturnMapName, *ReturnMapSource);

  const bool bHasPendingResolution =
      GI && GI->bPendingBattleResolution && GI->PendingBattleResolution.bValid;
  if (bHasPendingResolution) {
    UE_LOG(LogSkald, Verbose,
           TEXT("HandleGridBattleEnded: reusing already captured pending battle resolution."));
  } else {
    ResolveGridBattleResult();
  }

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

bool ATurnManager::ResolveBattleReturnMapName(FString &OutReturnMapName,
                                              FString &OutReturnMapSource) const {
  OutReturnMapName.Reset();
  OutReturnMapSource.Reset();

  UWorld *World = GetWorld();
  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();

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
             TEXT("ResolveBattleReturnMapName: %s value '%s' is not a valid long package name."),
             SourceLabel, *Candidate);
      return false;
    }

    OutReturnMapName = MoveTemp(Canonical);
    OutReturnMapSource = SourceLabel;
    return true;
  };

  if (TryResolveReturnMap(PendingBattle.ReturnMap, TEXT("PendingBattle.ReturnMap"))) {
    return true;
  }

  if (GI) {
    if (TryResolveReturnMap(GI->PendingBattle.ReturnMap,
                            TEXT("GameInstance.PendingBattle.ReturnMap"))) {
      return true;
    }
    if (TryResolveReturnMap(GI->GetPendingReturnMap(),
                            TEXT("GameInstance.PendingReturnMap"))) {
      return true;
    }
  }

  const FString FallbackMap = GetFallbackOverviewMapPackageName();
  return TryResolveReturnMap(FallbackMap, TEXT("FallbackOverviewMap"));
}

bool ATurnManager::ResolveBattleReturnMapNameForTesting(
    FString &OutReturnMapName, FString &OutReturnMapSource) const {
  return ResolveBattleReturnMapName(OutReturnMapName, OutReturnMapSource);
}

void ATurnManager::HandleBattleMapStateChanged(bool bInBattleMap) {
  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    if (bInBattleMap) {
      AttemptBindBattleEnd(GI);
    } else {
      ClearBattleEndBinding(GI);
    }
  }

  if (!bInBattleMap) {
    BroadcastPrepareForBattlePrompt(PendingBattlePreparation,
                                    TEXT("HandleBattleMapStateChanged"));
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
  if (IsValid(Controller) && !Controllers.Contains(Controller)) {
    Controllers.Add(Controller);
    Controller->SetTurnManager(this);
    if (UWorld *World = GetWorld()) {
      if (ASkaldGameState *GameState = World->GetGameState<ASkaldGameState>()) {
        PendingBattleReadyState = GameState->GetPendingBattleReady();
      }
    }

    if (PendingBattlePreparation.FromTerritoryID != 0 ||
        PendingBattlePreparation.TargetTerritoryID != 0) {
      BroadcastPrepareForBattlePrompt(PendingBattlePreparation,
                                      TEXT("RegisterController"));
    }
  }
}

void ATurnManager::RestoreControllerOrderFromSnapshots(const TArray<FS_PlayerData> &Snapshots) {
  if (Snapshots.Num() == 0 || Controllers.Num() == 0) {
    return;
  }

  auto NormaliseName = [](const FString &InName) {
    FString Result = InName;
    Result.TrimStartAndEndInline();
    Result.ToLowerInline();
    return Result;
  };

  TMap<int32, int32> DesiredIndexById;
  TMap<FString, int32> DesiredIndexByName;

  for (const FS_PlayerData &Snapshot : Snapshots) {
    const int32 DesiredIndex = (Snapshot.DesiredControllerIndex >= 0)
                                   ? Snapshot.DesiredControllerIndex
                                   : Snapshot.DesiredTurnIndex;
    if (DesiredIndex < 0) {
      continue;
    }

    if (Snapshot.PlayerID > 0 && !DesiredIndexById.Contains(Snapshot.PlayerID)) {
      DesiredIndexById.Add(Snapshot.PlayerID, DesiredIndex);
    }

    const FString NormalisedName = NormaliseName(!Snapshot.DisplayName.IsEmpty()
                                                     ? Snapshot.DisplayName
                                                     : Snapshot.PlayerName);
    if (!NormalisedName.IsEmpty() && !DesiredIndexByName.Contains(NormalisedName)) {
      DesiredIndexByName.Add(NormalisedName, DesiredIndex);
    }
  }

  TArray<TWeakObjectPtr<ASkaldPlayerController>> Reordered;
  Reordered.SetNum(Controllers.Num());
  TArray<TWeakObjectPtr<ASkaldPlayerController>> Unplaced;
  Unplaced.Reserve(Controllers.Num());

  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr : Controllers) {
    ASkaldPlayerController *Controller = ControllerPtr.Get();
    if (!Controller) {
      Unplaced.Add(ControllerPtr);
      continue;
    }

    int32 DesiredIndex = INDEX_NONE;
    if (ASkaldPlayerState *PS = Controller->GetPlayerState<ASkaldPlayerState>()) {
      if (PS->GetPlayerId() > 0) {
        if (int32 *FoundIndex = DesiredIndexById.Find(PS->GetPlayerId())) {
          DesiredIndex = *FoundIndex;
        }
      }

      if (DesiredIndex == INDEX_NONE) {
        const FString Normalised = NormaliseName(PS->PlayerDisplayName.IsEmpty()
                                                     ? PS->GetPlayerName()
                                                     : PS->PlayerDisplayName);
        if (!Normalised.IsEmpty()) {
          if (int32 *FoundIndex = DesiredIndexByName.Find(Normalised)) {
            DesiredIndex = *FoundIndex;
          }
        }
      }
    }

    if (DesiredIndex != INDEX_NONE && Reordered.IsValidIndex(DesiredIndex) &&
        !Reordered[DesiredIndex].IsValid()) {
      Reordered[DesiredIndex] = ControllerPtr;
    } else {
      Unplaced.Add(ControllerPtr);
    }
  }

  int32 UnplacedIndex = 0;
  for (int32 Index = 0; Index < Reordered.Num(); ++Index) {
    if (!Reordered[Index].IsValid() && Unplaced.IsValidIndex(UnplacedIndex)) {
      Reordered[Index] = Unplaced[UnplacedIndex++];
    }
  }

  for (; UnplacedIndex < Unplaced.Num(); ++UnplacedIndex) {
    Reordered.Add(Unplaced[UnplacedIndex]);
  }

  Controllers = MoveTemp(Reordered);
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

  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr :
       Controllers) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      if (ASkaldPlayerState *PS = Controller->GetPlayerState<ASkaldPlayerState>()) {
        PS->ResetArmyPlacementDeployments();
      }
    }
  }

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
    int32 NewActivePlayerId = INDEX_NONE;
    ASkaldPlayerState *ActivePlayerState = nullptr;
    if (Controllers.IsValidIndex(CurrentIndex) &&
        Controllers[CurrentIndex].IsValid()) {
      if (ASkaldPlayerState *PS =
              Controllers[CurrentIndex]->GetPlayerState<ASkaldPlayerState>()) {
        NewIndex = GS->FindTurnIndexForStableId(PS->GetStablePlayerId());
        if (NewIndex == INDEX_NONE) {
          NewIndex = GS->PlayerArray.IndexOfByKey(PS);
        }
        NewActivePlayerId = PS->GetStablePlayerId();
        ActivePlayerState = PS;
      }
    }

    // Avoid thrashing the replicated ActivePlayerId while players reconnect to
    // the overview map after travel. If we previously had a valid active
    // player, keep broadcasting that stable value until the controllers have
    // fully re-registered and we can resolve a new one.
    if (!ActivePlayerState && GS->ActivePlayerId != INDEX_NONE) {
      return;
    }
    GS->CurrentTurnIndex = NewIndex;
    GS->SetActivePlayerId(NewActivePlayerId);
    UE_LOG(LogSkald, Log,
           TEXT("[TurnState] SyncGameStateTurnIndex -> Index=%d StableId=%d"),
           NewIndex, NewActivePlayerId);
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
  // SavedTurnPlayerId stores the stable player identifier for the active turn
  // participant when we left the overview map.
  const int32 SavedStableId = GI->SavedTurnPlayerId;

  auto ResolveIndexForStableId = [&](int32 PlayerId) -> int32 {
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
          if (PS->GetStablePlayerId() == PlayerId) {
            return Index;
          }
        }
      }
    }
    return INDEX_NONE;
  };

  int32 TargetIndex = ResolveIndexForStableId(SavedStableId);
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

  BroadcastPrepareForBattlePrompt(PendingBattlePreparation,
                                  TEXT("TryResumeSavedTurnState"));

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
  ApplyReinforcementsAndResources(PS, TEXT("StartTurns"));

  CurrentPhase = ETurnPhase::Reinforcement;

  // Mirror the replicated phase change path locally so the listen server host
  // refreshes HUD, camera, and turn ownership from replicated state rather
  // than host-only UI calls. Remote clients will respond via
  // OnRep_CurrentPhase.
  BroadcastCurrentPhase();

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

  const int32 PreviousIndex = CurrentIndex;

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

  if (FoundIndex != INDEX_NONE) {
    CurrentIndex = (FoundIndex + 1) % Controllers.Num();
  } else {
    const int32 SafeCount = Controllers.Num();
    if (SafeCount <= 0) {
      return;
    }

    int32 WrappedIndex = PreviousIndex % SafeCount;
    if (WrappedIndex < 0) {
      WrappedIndex += SafeCount;
    }

    CurrentIndex = WrappedIndex;
  }
  if (ASkaldPlayerController *CurrentController =
          Controllers[CurrentIndex].Get()) {
    ASkaldPlayerState *PS =
        CurrentController->GetPlayerState<ASkaldPlayerState>();
    ResetMovementActionsForActivePlayer();
    ApplyReinforcementsAndResources(PS, TEXT("AdvanceTurn"));

    CurrentPhase = ETurnPhase::Reinforcement;

    // Phase replication will drive HUD/turn updates on clients; mirror that
    // path locally for the listen server host without issuing host-only RPCs.
    BroadcastCurrentPhase();

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

bool ATurnManager::CanPerformMovementAction(int32 PlayerID,
                                            FString *OutError) const {
  if (PlayerID <= 0) {
    if (OutError) {
      *OutError = TEXT("Unable to resolve player for movement.");
    }
    return false;
  }

  const int32 Used = MovementActionsTaken.FindRef(PlayerID);
  if (Used >= MaxMovementActionsPerMovementPhase) {
    if (OutError) {
      *OutError =
          TEXT("You can only move troops twice during the movement phase.");
    }
    return false;
  }

  return true;
}

void ATurnManager::RecordMovementAction(int32 PlayerID) {
  if (PlayerID <= 0) {
    return;
  }

  int32 &Used = MovementActionsTaken.FindOrAdd(PlayerID);
  Used += 1;
}

int32 ATurnManager::GetMovementActionsRemaining(int32 PlayerID) const {
  if (PlayerID <= 0) {
    return 0;
  }

  const int32 Used = MovementActionsTaken.FindRef(PlayerID);
  return FMath::Max(0, MaxMovementActionsPerMovementPhase - Used);
}

void ATurnManager::ResetMovementActionsForActivePlayer() {
  const int32 ActivePlayerId = GetActivePlayerId();
  if (ActivePlayerId != INDEX_NONE) {
    MovementActionsTaken.Remove(ActivePlayerId);
  }
}

void ATurnManager::SetMovementActionsSnapshot(const TMap<int32, int32> &InActions)
{
  MovementActionsTaken = InActions;
}

void ATurnManager::SetPendingBattlePayload(const FS_BattlePayload &Battle)
{
  PendingBattle = Battle;
}

void ATurnManager::SetPendingBattlePreparation(const FS_BattlePayload &Battle)
{
  PendingBattlePreparation = Battle;
}

void ATurnManager::SetPendingBattleReadyState(const FSkaldBattleReadyState &ReadyState)
{
  PendingBattleReadyState = ReadyState;
}

int32 ATurnManager::GetActivePlayerId() const {
  if (!Controllers.IsValidIndex(CurrentIndex)) {
    return INDEX_NONE;
  }

  if (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr =
          Controllers[CurrentIndex];
      ControllerPtr.IsValid()) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      if (ASkaldPlayerState *PS = Controller->GetPlayerState<ASkaldPlayerState>()) {
        return PS->GetStablePlayerId();
      }
    }
  }

  return INDEX_NONE;
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

void ATurnManager::ClearBattleResultAcknowledgements() {
  PendingBattleResultAckPlayerIds.Reset();
  bAwaitingBattleResultAcknowledgements = false;
}

bool ATurnManager::BeginBattleResultAcknowledgementWindow() {
  ClearBattleResultAcknowledgements();

  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr : Controllers) {
    ASkaldPlayerController *Controller = ControllerPtr.Get();
    if (!Controller) {
      continue;
    }

    ASkaldPlayerState *PS = Controller->GetPlayerState<ASkaldPlayerState>();
    if (!PS || PS->bIsAI) {
      continue;
    }

    const int32 StableId = PS->GetStablePlayerId();
    const int32 PlayerId = StableId > 0 ? StableId : PS->GetPlayerId();
    if (PlayerId > 0) {
      PendingBattleResultAckPlayerIds.Add(PlayerId);
    }
  }

  bAwaitingBattleResultAcknowledgements =
      PendingBattleResultAckPlayerIds.Num() > 0;
  return bAwaitingBattleResultAcknowledgements;
}

bool ATurnManager::IsCurrentControllerAI() const {
  if (!Controllers.IsValidIndex(CurrentIndex)) {
    return false;
  }

  if (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr =
          Controllers[CurrentIndex];
      ControllerPtr.IsValid()) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      if (ASkaldPlayerState *PS = Controller->GetPlayerState<ASkaldPlayerState>()) {
        return PS->bIsAI;
      }
    }
  }

  return false;
}

void ATurnManager::NotifyBattleResultAcknowledged(int32 PlayerID) {
  if (!HasAuthority() || PlayerID <= 0) {
    return;
  }

  if (!bAwaitingBattleResultAcknowledgements) {
    return;
  }

  PendingBattleResultAckPlayerIds.Remove(PlayerID);
  if (PendingBattleResultAckPlayerIds.Num() == 0) {
    bAwaitingBattleResultAcknowledgements = false;
    PendingBattleResultAckPlayerIds.Reset();
    UE_LOG(LogSkald, Verbose,
           TEXT("Battle result acknowledgements complete; resuming AI decisions."));
  } else {
    UE_LOG(LogSkald, Verbose,
           TEXT("Battle result acknowledgement received from StablePlayerID=%d; Remaining=%d"),
           PlayerID, PendingBattleResultAckPlayerIds.Num());
  }
}

bool ATurnManager::HasPendingBattlePreparation() const {
  const bool bHasPayload = PendingBattlePreparation.FromTerritoryID != 0 ||
                           PendingBattlePreparation.TargetTerritoryID != 0;
  const bool bHasReadyAssignments =
      PendingBattleReadyState.AttackerPlayerID != INDEX_NONE ||
      PendingBattleReadyState.DefenderPlayerID != INDEX_NONE ||
      PendingBattleReadyState.bAttackerReady ||
      PendingBattleReadyState.bDefenderReady;

  const bool bRetreatInProgress = ActiveRetreatContext.IsActive();

  return bHasPayload || bHasReadyAssignments || bRetreatInProgress;
}

static FString GCurrentTravelSessionToken;

void ATurnManager::HandleAttackConfirmed(const FS_BattlePayload &Battle) {
  if (GCurrentTravelSessionToken.IsEmpty()) { GCurrentTravelSessionToken = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower); }
  UE_LOG(LogSkald, Log, TEXT("[TravelToken] Token=%s Stage=Create World=%s Ctx=HandleAttackConfirmed PayloadValid=1"), *GCurrentTravelSessionToken, *GetNameSafe(GetWorld()));
  UE_LOG(LogSkaldReady, Log,
         TEXT("AttackConfirmed From=%d To=%d Attacker=%d Defender=%d"),
         Battle.FromTerritoryID, Battle.TargetTerritoryID,
         Battle.AttackerPlayerID, Battle.DefenderPlayerID);

  if (USkaldGameInstance* GI = GetGameInstance<USkaldGameInstance>()) { GI->bTurnStateFrozenForTravel = true; UE_LOG(LogSkald, Log, TEXT("[TurnFreeze] Ctx=HandleAttackConfirmed Frozen=1 ActiveId=%d LocalTurnActive=0 Action=skip"), GetActivePlayerId()); }
  BeginReadyPhase(Battle, TEXT("HandleAttackConfirmed"));
}

void ATurnManager::RequestDefenderRetreat(
    ASkaldPlayerController *RequestingController) {
  if (!HasAuthority()) {
    return;
  }

  if (!RequestingController) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("RequestDefenderRetreat ignored: requesting controller missing."));
    return;
  }

  if (ActiveRetreatContext.IsActive()) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("RequestDefenderRetreat ignored: retreat already in progress."));
    const FText AlreadyInProgressMessage =
        NSLOCTEXT("SkaldHUD", "RetreatAlreadyPending", "Cannot Retreat!");
    const bool bIsLocalController = RequestingController->IsLocalController();
    const bool bHasClientConnection =
        !bIsLocalController && RequestingController->GetNetConnection() != nullptr;
    if (bIsLocalController || !bHasClientConnection) {
      RequestingController->NotifyRetreatFailed(AlreadyInProgressMessage);
    }
    if (bHasClientConnection) {
      RequestingController->ClientRetreatFailed(AlreadyInProgressMessage);
    }
    return;
  }

  const int32 DefendingPlayerId = PendingBattlePreparation.DefenderPlayerID;
  const bool bHasPendingBattle =
      PendingBattlePreparation.FromTerritoryID > 0 &&
      PendingBattlePreparation.TargetTerritoryID > 0 &&
      DefendingPlayerId != INDEX_NONE;

  if (!bHasPendingBattle) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("RequestDefenderRetreat ignored: no pending battle preparation."));
    const FText NoPendingBattleMessage =
        NSLOCTEXT("SkaldHUD", "RetreatNoPendingBattle", "Cannot Retreat!");
    const bool bIsLocalController = RequestingController->IsLocalController();
    const bool bHasClientConnection =
        !bIsLocalController && RequestingController->GetNetConnection() != nullptr;
    if (bIsLocalController || !bHasClientConnection) {
      RequestingController->NotifyRetreatFailed(NoPendingBattleMessage);
    }
    if (bHasClientConnection) {
      RequestingController->ClientRetreatFailed(NoPendingBattleMessage);
    }
    return;
  }

  ASkaldPlayerState *RequestingState =
      RequestingController->GetPlayerState<ASkaldPlayerState>();
  const int32 RequestingId = ResolveStableOrPlayerId(RequestingState);
  if (!RequestingState || RequestingId != DefendingPlayerId) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("RequestDefenderRetreat rejected: controller %s is not the defender."),
           *GetNameSafe(RequestingController));
    const FText NotDefenderMessage =
        NSLOCTEXT("SkaldHUD", "RetreatNotDefender", "Cannot Retreat!");
    const bool bIsLocalController = RequestingController->IsLocalController();
    const bool bHasClientConnection =
        !bIsLocalController && RequestingController->GetNetConnection() != nullptr;
    if (bIsLocalController || !bHasClientConnection) {
      RequestingController->NotifyRetreatFailed(NotDefenderMessage);
    }
    if (bHasClientConnection) {
      RequestingController->ClientRetreatFailed(NotDefenderMessage);
    }
    return;
  }

  AWorldMap *WorldMap = ResolveWorldMap();
  if (!WorldMap) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("RequestDefenderRetreat failed: world map unavailable."));
    const FText FailureMessage =
        NSLOCTEXT("SkaldHUD", "RetreatWorldMissing", "Cannot Retreat!");
    const bool bIsLocalController = RequestingController->IsLocalController();
    const bool bHasClientConnection =
        !bIsLocalController && RequestingController->GetNetConnection() != nullptr;
    if (bIsLocalController || !bHasClientConnection) {
      RequestingController->NotifyRetreatFailed(FailureMessage);
    }
    if (bHasClientConnection) {
      RequestingController->ClientRetreatFailed(FailureMessage);
    }
    return;
  }

  ATerritory *DefendingTerritory =
      WorldMap->GetTerritoryById(PendingBattlePreparation.TargetTerritoryID);
  if (!DefendingTerritory) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("RequestDefenderRetreat failed: defending territory %d not found."),
           PendingBattlePreparation.TargetTerritoryID);
    const FText FailureMessage =
        NSLOCTEXT("SkaldHUD", "RetreatNoTerritory", "Cannot Retreat!");
    const bool bIsLocalController = RequestingController->IsLocalController();
    const bool bHasClientConnection =
        !bIsLocalController && RequestingController->GetNetConnection() != nullptr;
    if (bIsLocalController || !bHasClientConnection) {
      RequestingController->NotifyRetreatFailed(FailureMessage);
    }
    if (bHasClientConnection) {
      RequestingController->ClientRetreatFailed(FailureMessage);
    }
    return;
  }

  TSet<int32> CandidateIds;
  for (ATerritory *Adjacent : DefendingTerritory->AdjacentTerritories) {
    if (Adjacent && Adjacent->OwningPlayer == RequestingState) {
      CandidateIds.Add(Adjacent->TerritoryID);
    }
  }

  if (CandidateIds.Num() == 0) {
    UE_LOG(LogSkaldReady, Log,
           TEXT("RequestDefenderRetreat rejected: defender %s has no adjacent territory to retreat to."),
           *GetNameSafe(RequestingController));
    const FText NoAdjacentMessage =
        NSLOCTEXT("SkaldHUD", "RetreatNoAdjacent", "Cannot Retreat!");
    const bool bIsLocalController = RequestingController->IsLocalController();
    const bool bHasClientConnection =
        !bIsLocalController && RequestingController->GetNetConnection() != nullptr;
    if (bIsLocalController || !bHasClientConnection) {
      RequestingController->NotifyRetreatFailed(NoAdjacentMessage);
    }
    if (bHasClientConnection) {
      RequestingController->ClientRetreatFailed(NoAdjacentMessage);
    }
    return;
  }

  ActiveRetreatContext.Reset();
  ActiveRetreatContext.BattlePayload = PendingBattlePreparation;
  ActiveRetreatContext.AttackerController =
      FindControllerByPlayerId(PendingBattlePreparation.AttackerPlayerID);
  ActiveRetreatContext.DefenderController = RequestingController;
  ActiveRetreatContext.CandidateTerritoryIds = MoveTemp(CandidateIds);
  ActiveRetreatContext.DefendingTerritoryId =
      PendingBattlePreparation.TargetTerritoryID;
  ActiveRetreatContext.AttackingTerritoryId =
      PendingBattlePreparation.FromTerritoryID;
  ActiveRetreatContext.bAwaitingDestination = true;

  PendingBattlePreparation = FS_BattlePayload();
  PendingBattleReadyState = FSkaldBattleReadyState();
  CommitPendingBattleReadyState(TEXT("RequestDefenderRetreat_ClearReady"));

  auto DispatchToController =
      [](ASkaldPlayerController *Controller,
         TFunctionRef<void(ASkaldPlayerController *)> LocalAction,
         TFunctionRef<void(ASkaldPlayerController *)> ClientAction) {
        if (!Controller) {
          return;
        }
        const bool bIsLocal = Controller->IsLocalController();
        const bool bHasClientConnection =
            !bIsLocal && Controller->GetNetConnection() != nullptr;
        const bool bShouldExecuteLocal = bIsLocal || !bHasClientConnection;
        if (bShouldExecuteLocal) {
          LocalAction(Controller);
        }
        if (bHasClientConnection) {
          ClientAction(Controller);
        }
      };

  auto HidePreparePrompt = [&](ASkaldPlayerController *Controller) {
    DispatchToController(
        Controller,
        [](ASkaldPlayerController *LocalController) {
          LocalController->HidePrepareForBattlePromptLocal();
        },
        [](ASkaldPlayerController *RemoteController) {
          RemoteController->ClientHidePrepareForBattle();
        });
  };

  auto NotifyEnemyRetreated = [&](ASkaldPlayerController *Controller) {
    DispatchToController(
        Controller,
        [](ASkaldPlayerController *LocalController) {
          LocalController->NotifyEnemyRetreated();
        },
        [](ASkaldPlayerController *RemoteController) {
          RemoteController->ClientEnemyRetreated();
        });
  };

  auto BeginSelectionForController = [&](ASkaldPlayerController *Controller) {
    if (!Controller) {
      return;
    }
    TArray<int32> CandidateArray =
        ActiveRetreatContext.CandidateTerritoryIds.Array();
    DispatchToController(
        Controller,
        [&](ASkaldPlayerController *LocalController) {
          LocalController->BeginRetreatSelectionLocal(
              ActiveRetreatContext.DefendingTerritoryId, CandidateArray);
        },
        [&](ASkaldPlayerController *RemoteController) {
          RemoteController->ClientBeginRetreatSelection(
              ActiveRetreatContext.DefendingTerritoryId, CandidateArray);
        });
  };

  ASkaldPlayerController *AttackerController =
      ActiveRetreatContext.AttackerController.Get();

  HidePreparePrompt(RequestingController);

  if (AttackerController) {
    NotifyEnemyRetreated(AttackerController);
  }

  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr :
       Controllers) {
    ASkaldPlayerController *Controller = ControllerPtr.Get();
    if (!Controller || Controller == RequestingController ||
        Controller == AttackerController) {
      continue;
    }

    NotifyEnemyRetreated(Controller);
  }

  BeginSelectionForController(RequestingController);

  UE_LOG(LogSkaldReady, Log,
         TEXT("Defender %s initiated retreat from territory %d."),
         *GetNameSafe(RequestingController),
         ActiveRetreatContext.DefendingTerritoryId);
}

void ATurnManager::ConfirmDefenderRetreatDestination(
    ASkaldPlayerController *RequestingController, int32 TerritoryID) {
  if (!HasAuthority()) {
    return;
  }

  if (!ActiveRetreatContext.IsActive()) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("ConfirmDefenderRetreatDestination ignored: no active retreat."));
    return;
  }

  if (!RequestingController) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("ConfirmDefenderRetreatDestination ignored: missing controller."));
    return;
  }

  ASkaldPlayerState *RequestingState =
      RequestingController->GetPlayerState<ASkaldPlayerState>();
  const int32 DefenderPlayerId =
      ActiveRetreatContext.BattlePayload.DefenderPlayerID;
  if (!RequestingState || RequestingState->GetPlayerId() != DefenderPlayerId) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("ConfirmDefenderRetreatDestination rejected: controller %s is not the defender."),
           *GetNameSafe(RequestingController));
    return;
  }

  if (!ActiveRetreatContext.CandidateTerritoryIds.Contains(TerritoryID)) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("ConfirmDefenderRetreatDestination rejected: territory %d not a valid retreat candidate."),
           TerritoryID);
    const FText InvalidSelectionMessage = NSLOCTEXT(
        "SkaldHUD", "RetreatInvalidSelection",
        "Select a highlighted territory to retreat to.");
    const bool bIsLocalController = RequestingController->IsLocalController();
    const bool bHasClientConnection =
        !bIsLocalController && RequestingController->GetNetConnection() != nullptr;
    if (bIsLocalController || !bHasClientConnection) {
      RequestingController->NotifyRetreatFailed(InvalidSelectionMessage);
    }
    if (bHasClientConnection) {
      RequestingController->ClientRetreatFailed(InvalidSelectionMessage);
    }
    return;
  }

  AWorldMap *WorldMap = ResolveWorldMap();
  if (!WorldMap) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("ConfirmDefenderRetreatDestination failed: world map unavailable."));
    const FText FailureMessage =
        NSLOCTEXT("SkaldHUD", "RetreatWorldMissingConfirm", "Cannot Retreat!");
    const bool bIsLocalController = RequestingController->IsLocalController();
    const bool bHasClientConnection =
        !bIsLocalController && RequestingController->GetNetConnection() != nullptr;
    if (bIsLocalController || !bHasClientConnection) {
      RequestingController->NotifyRetreatFailed(FailureMessage);
    }
    if (bHasClientConnection) {
      RequestingController->ClientRetreatFailed(FailureMessage);
    }
    return;
  }

  ATerritory *DefendingTerritory =
      WorldMap->GetTerritoryById(ActiveRetreatContext.DefendingTerritoryId);
  ATerritory *Destination = WorldMap->GetTerritoryById(TerritoryID);
  ATerritory *AttackerSource =
      WorldMap->GetTerritoryById(ActiveRetreatContext.AttackingTerritoryId);

  if (!DefendingTerritory || !Destination) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("ConfirmDefenderRetreatDestination failed: missing defending (%s) or destination (%s) territory."),
           *GetNameSafe(DefendingTerritory), *GetNameSafe(Destination));
    const FText FailureMessage =
        NSLOCTEXT("SkaldHUD", "RetreatInvalidTerritory", "Cannot Retreat!");
    const bool bIsLocalController = RequestingController->IsLocalController();
    const bool bHasClientConnection =
        !bIsLocalController && RequestingController->GetNetConnection() != nullptr;
    if (bIsLocalController || !bHasClientConnection) {
      RequestingController->NotifyRetreatFailed(FailureMessage);
    }
    if (bHasClientConnection) {
      RequestingController->ClientRetreatFailed(FailureMessage);
    }
    return;
  }

  if (Destination->OwningPlayer != RequestingState) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("ConfirmDefenderRetreatDestination rejected: destination territory %d is no longer owned by defender."),
           TerritoryID);
    const FText OwnershipMessage =
        NSLOCTEXT("SkaldHUD", "RetreatLostTerritory", "Cannot Retreat!");
    const bool bIsLocalController = RequestingController->IsLocalController();
    const bool bHasClientConnection =
        !bIsLocalController && RequestingController->GetNetConnection() != nullptr;
    if (bIsLocalController || !bHasClientConnection) {
      RequestingController->NotifyRetreatFailed(OwnershipMessage);
    }
    if (bHasClientConnection) {
      RequestingController->ClientRetreatFailed(OwnershipMessage);
    }
    return;
  }

  const int32 AttackerPlayerId =
      ActiveRetreatContext.BattlePayload.AttackerPlayerID;
  ASkaldPlayerController *AttackerController =
      ActiveRetreatContext.AttackerController.Get();
  ASkaldPlayerState *AttackerState = nullptr;
  if (AttackerController) {
    AttackerState = AttackerController->GetPlayerState<ASkaldPlayerState>();
  }

  if (!AttackerState) {
    if (ASkaldGameState *GameState =
            GetWorld() ? GetWorld()->GetGameState<ASkaldGameState>() : nullptr) {
      AttackerState = GameState->GetPlayerById(AttackerPlayerId);
    }
  }

  const int32 DefenderArmy = FMath::Max(0, DefendingTerritory->ArmyUnits);
  const int32 AttackerArmySent =
      FMath::Max(0, ActiveRetreatContext.BattlePayload.ArmyCountSent);

  Destination->ArmyUnits += DefenderArmy;
  Destination->RefreshAppearance();
  DefendingTerritory->ArmyUnits = 0;

  if (AttackerSource) {
    int32 ArmyToMove = AttackerArmySent;
    if (ArmyToMove > AttackerSource->ArmyUnits) {
      UE_LOG(LogSkaldReady, Warning,
             TEXT("Retreat army adjustment: attacker source only has %d units but %d were committed."),
             AttackerSource->ArmyUnits, ArmyToMove);
      ArmyToMove = AttackerSource->ArmyUnits;
    }
    AttackerSource->ArmyUnits -= ArmyToMove;
    AttackerSource->RefreshAppearance();
    DefendingTerritory->ArmyUnits = ArmyToMove;
  } else {
    DefendingTerritory->ArmyUnits = AttackerArmySent;
  }

  DefendingTerritory->OwningPlayer = AttackerState;
  DefendingTerritory->RefreshAppearance();

  if (USkaldGameInstance *GameInstance = GetGameInstance<USkaldGameInstance>()) {
    GameInstance->PendingBattle = FS_BattlePayload();
  }
  PendingBattle = FS_BattlePayload();

  auto DispatchToControllerConfirm =
      [](ASkaldPlayerController *Controller,
         TFunctionRef<void(ASkaldPlayerController *)> LocalAction,
         TFunctionRef<void(ASkaldPlayerController *)> ClientAction) {
        if (!Controller) {
          return;
        }
        const bool bIsLocal = Controller->IsLocalController();
        const bool bHasClientConnection =
            !bIsLocal && Controller->GetNetConnection() != nullptr;
        const bool bShouldExecuteLocal = bIsLocal || !bHasClientConnection;
        if (bShouldExecuteLocal) {
          LocalAction(Controller);
        }
        if (bHasClientConnection) {
          ClientAction(Controller);
        }
      };

  DispatchToControllerConfirm(
      RequestingController,
      [](ASkaldPlayerController *LocalController) {
        LocalController->CompleteRetreatSelectionLocal();
      },
      [](ASkaldPlayerController *RemoteController) {
        RemoteController->ClientCompleteRetreat();
      });

  DispatchToControllerConfirm(
      RequestingController,
      [](ASkaldPlayerController *LocalController) {
        LocalController->HandleWorldStateChanged();
      },
      [](ASkaldPlayerController *RemoteController) {
        RemoteController->HandleWorldStateChanged();
      });

  if (AttackerController) {
    DispatchToControllerConfirm(
        AttackerController,
        [](ASkaldPlayerController *LocalController) {
          LocalController->HandleWorldStateChanged();
        },
        [](ASkaldPlayerController *RemoteController) {
          RemoteController->HandleWorldStateChanged();
        });
  }

  const int32 CapturedTerritoryId = ActiveRetreatContext.DefendingTerritoryId;
  ClearActiveRetreatContext();

  if (ASkaldGameMode *GameMode =
          GetWorld() ? GetWorld()->GetAuthGameMode<ASkaldGameMode>() : nullptr) {
    GameMode->CheckVictoryConditions();
  }

  OnWorldStateChanged.Broadcast();

  UE_LOG(LogSkaldReady, Log,
         TEXT("Defender retreated to territory %d. Attacker claimed territory %d."),
         TerritoryID, CapturedTerritoryId);
}

void ATurnManager::RequestPrepareBattle(const FS_BattlePayload &Battle) {
  BeginReadyPhase(Battle, TEXT("RequestPrepareBattle"));
}

void ATurnManager::MarkParticipantActive(ASkaldPlayerState *Participant) const
{
  if (!Participant)
  {
    return;
  }

  Participant->bIsActiveBattlePlayer = true;
  UE_LOG(LogSkaldBattle, Log, TEXT("MarkParticipantActive PlayerId=%d Name=%s"),
         Participant->GetPlayerId(),
         *Participant->GetResolvedPlayerName(TEXT("MarkParticipantActive")));
}

void ATurnManager::CacheBattleParticipants(const FS_BattlePayload &Battle)
{
  if (!HasAuthority())
  {
    return;
  }

  ASkaldGameState *GameState = GetWorld() ? GetWorld()->GetGameState<ASkaldGameState>() : nullptr;
  if (!GameState)
  {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("CacheBattleParticipants aborted: GameState missing (From=%d To=%d Attacker=%d Defender=%d)."),
           Battle.FromTerritoryID, Battle.TargetTerritoryID, Battle.AttackerPlayerID,
           Battle.DefenderPlayerID);
    return;
  }

  for (APlayerState *PlayerState : GameState->PlayerArray)
  {
    if (ASkaldPlayerState *SkaldPS = Cast<ASkaldPlayerState>(PlayerState))
    {
      SkaldPS->bIsActiveBattlePlayer = false;
    }
  }

  GameState->ResetBattleParticipants();

  auto BuildEntry = [&](ASkaldPlayerState *Participant, int32 PlayerId, const FString &DisplayName,
                        ESkaldFaction Faction, bool bIsAI, bool bResolvedByStableId, int32 PendingBudget)
  {
    FBattlePlayerEntry Entry;
    Entry.PlayerId = bResolvedByStableId && Participant ? PlayerId
                                                        : Participant ? Participant->GetPlayerId()
                                                                      : PlayerId;
    Entry.DisplayName = Participant ? Participant->GetResolvedPlayerName(TEXT("CacheBattleParticipants")) : DisplayName;
    Entry.Faction = Participant && Participant->Faction != ESkaldFaction::None ? Participant->Faction : Faction;
    Entry.bIsAI = Participant ? Participant->bIsAI : bIsAI;
    Entry.PendingArmyBudget = PendingBudget;

    UE_LOG(LogSkaldBattle, Log,
           TEXT("CacheBattleParticipants: PlayerId=%d Name=%s Faction=%d AI=%s"),
           Entry.PlayerId, *Entry.DisplayName, static_cast<int32>(Entry.Faction),
           Entry.bIsAI ? TEXT("true") : TEXT("false"));

    GameState->UpsertBattleEntry(Entry);
    MarkParticipantActive(Participant);
  };

  auto ResolveParticipantStateByStableId = [GameState](int32 StableId,
                                                      const TCHAR *RoleLabel)
      -> ASkaldPlayerState * {
    ASkaldPlayerState *ByStable = GameState->GetPlayerByStableId(StableId);
    if (ByStable)
    {
      UE_LOG(LogSkaldBattle, Log,
             TEXT("CacheBattleParticipants: Resolved %s via StableId=%d -> %s"),
             RoleLabel, StableId,
             *ByStable->GetResolvedPlayerName(TEXT("CacheBattleParticipants.Stable")));
    }
    return ByStable;
  };

  ASkaldPlayerState *Attacker = GameState->GetPlayerById(Battle.AttackerPlayerID);
  const bool bAttackerResolvedByStableId = !Attacker;
  if (!Attacker)
  {
    Attacker = ResolveParticipantStateByStableId(Battle.AttackerPlayerID, TEXT("Attacker"));
  }

  ASkaldPlayerState *Defender = GameState->GetPlayerById(Battle.DefenderPlayerID);
  const bool bDefenderResolvedByStableId = !Defender;
  if (!Defender)
  {
    Defender = ResolveParticipantStateByStableId(Battle.DefenderPlayerID, TEXT("Defender"));
  }

  const int32 AttackerBudget = Battle.ArmyCountSent;
  const int32 DefenderBudget = Battle.DefenderArmyCount > 0 ? Battle.DefenderArmyCount : Battle.ArmyCountSent;

  BuildEntry(Attacker, Battle.AttackerPlayerID, Battle.AttackerDisplayName, Battle.AttackerFaction,
             Battle.bAttackerIsAI, bAttackerResolvedByStableId && Attacker != nullptr, AttackerBudget);
  BuildEntry(Defender, Battle.DefenderPlayerID, Battle.DefenderDisplayName, Battle.DefenderFaction,
             Battle.bDefenderIsAI, bDefenderResolvedByStableId && Defender != nullptr, DefenderBudget);
}

void ATurnManager::BeginReadyPhase(const FS_BattlePayload &Battle,
                                   const TCHAR *Context) {
  if (USkaldGameInstance* GI = GetGameInstance<USkaldGameInstance>()) { const FSkaldTravelState& TS = GI->GetTravelState(); const bool bComplete = TS.ExpectedControllers > 0 && TS.AttackerPlayerId > 0 && TS.DefenderPlayerId > 0 && TS.CachedTerritories.Num() > 0; UE_LOG(LogSkald, Log, TEXT("[TravelState] Validity=%s Expected=%d Attacker=%d Defender=%d CachedTerritories=%d"), bComplete ? TEXT("Complete") : (TS.bValid ? TEXT("Partial") : TEXT("Invalid")), TS.ExpectedControllers, TS.AttackerPlayerId, TS.DefenderPlayerId, TS.CachedTerritories.Num()); if (!bComplete) { UE_LOG(LogSkald, Warning, TEXT("[TravelState][WARN] Consumed non-complete state")); } }
  if (!HasAuthority()) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("BeginReadyPhase called without authority; ignoring."));
    return;
  }

  FS_BattlePayload NormalizedBattle = Battle;

  if (NormalizedBattle.DefenderArmyCount <= 0) {
    NormalizedBattle.DefenderArmyCount = NormalizedBattle.ArmyCountSent;
  }

  UE_LOG(LogSkaldReady, Log,
         TEXT("[BattlePrep] BeginReadyPhase Context=%s Attacker=%d Defender=%d From=%d To=%d AttackerBudget=%d DefenderBudget=%d"),
         Context ? Context : TEXT("BeginReadyPhase"), NormalizedBattle.AttackerPlayerID,
         NormalizedBattle.DefenderPlayerID, NormalizedBattle.FromTerritoryID,
         NormalizedBattle.TargetTerritoryID, NormalizedBattle.ArmyCountSent,
         NormalizedBattle.DefenderArmyCount);

  ClearActiveRetreatContext();

  USkaldGameInstance *GameInstance = GetGameInstance<USkaldGameInstance>();
  ASkaldGameState *GameState = GetWorld() ?
                                  GetWorld()->GetGameState<ASkaldGameState>()
                                  : nullptr;
  AWorldMap *WorldMap = ResolveWorldMap();

  auto ResolveParticipantState = [&](bool bIsAttacker) -> ASkaldPlayerState * {
    const int32 TerritoryId =
        bIsAttacker ? NormalizedBattle.FromTerritoryID
                    : NormalizedBattle.TargetTerritoryID;

    if (WorldMap && TerritoryId > 0) {
      if (ATerritory *Territory = WorldMap->GetTerritoryById(TerritoryId)) {
        if (ASkaldPlayerState *Owner = Territory->OwningPlayer) {
          return Owner;
        }
      }
    }

    const int32 PlayerId = bIsAttacker ? NormalizedBattle.AttackerPlayerID
                                       : NormalizedBattle.DefenderPlayerID;
    if (GameState && PlayerId > 0) {
      if (ASkaldPlayerState *ById = GameState->GetPlayerById(PlayerId)) {
        return ById;
      }
    }

    const FString ParticipantDisplayName =
        bIsAttacker ? NormalizedBattle.AttackerDisplayName
                    : NormalizedBattle.DefenderDisplayName;
    const ESkaldFaction ParticipantFaction =
        bIsAttacker ? NormalizedBattle.AttackerFaction
                    : NormalizedBattle.DefenderFaction;

    if (!GameState) {
      return nullptr;
    }

    if (!ParticipantDisplayName.IsEmpty()) {
      for (APlayerState *PlayerState : GameState->PlayerArray) {
        if (ASkaldPlayerState *SkaldPlayerState =
                Cast<ASkaldPlayerState>(PlayerState)) {
          FString CandidateName = SkaldPlayerState->PlayerDisplayName;
          if (CandidateName.IsEmpty()) {
            CandidateName = SkaldPlayerState->GetResolvedPlayerName(
                TEXT("BeginReadyPhaseNameMatch"));
          }

          if (!CandidateName.IsEmpty() &&
              CandidateName.Equals(ParticipantDisplayName,
                                   ESearchCase::IgnoreCase)) {
            return SkaldPlayerState;
          }
        }
      }
    }

    if (ParticipantFaction != ESkaldFaction::None) {
      ASkaldPlayerState *UniqueFactionMatch = nullptr;
      ASkaldPlayerState *UniqueAIFactionMatch = nullptr;
      int32 FactionMatchCount = 0;
      int32 FactionAIMatchCount = 0;

      for (APlayerState *PlayerState : GameState->PlayerArray) {
        if (ASkaldPlayerState *SkaldPlayerState =
                Cast<ASkaldPlayerState>(PlayerState)) {
          if (SkaldPlayerState->Faction != ParticipantFaction) {
            continue;
          }

          ++FactionMatchCount;
          if (!UniqueFactionMatch) {
            UniqueFactionMatch = SkaldPlayerState;
          }

          if (SkaldPlayerState->bIsAI) {
            ++FactionAIMatchCount;
            if (!UniqueAIFactionMatch) {
              UniqueAIFactionMatch = SkaldPlayerState;
            }
          }
        }
      }

      if (FactionMatchCount == 1 && UniqueFactionMatch) {
        return UniqueFactionMatch;
      }

      if (FactionMatchCount > 1 && UniqueAIFactionMatch &&
          FactionAIMatchCount == 1) {
        return UniqueAIFactionMatch;
      }
    }

    return nullptr;
  };

  ASkaldPlayerState *AttackerState = ResolveParticipantState(true);
  ASkaldPlayerState *DefenderState = ResolveParticipantState(false);

  const auto LogParticipantResolution = [&](const TCHAR *ParticipantRole,
                                            ASkaldPlayerState *Participant,
                                            int32 PlayerId) {
    if (Participant) {
      const int32 ResolvedId = ResolveStableOrPlayerId(Participant);
      if (PlayerId > 0 && ResolvedId != PlayerId) {
        UE_LOG(LogSkaldReady, Warning,
               TEXT("%s: %s resolved to PlayerId=%d but payload expected %d (potential slot mismatch)."),
               Context ? Context : TEXT("BeginReadyPhase"), ParticipantRole,
               ResolvedId, PlayerId);
      }
      return;
    }

    if (PlayerId > 0) {
      UE_LOG(LogSkaldReady, Warning,
             TEXT("%s: %s PlayerId %d missing during ready phase; clients may see ControlChannel close if state never registers."),
             Context ? Context : TEXT("BeginReadyPhase"), ParticipantRole,
             PlayerId);
    }
  };

  auto ApplyParticipantDetails =
      [&](ASkaldPlayerState *Participant, int32 &PlayerId, bool &bIsAI,
          FString &DisplayName, ESkaldFaction &Faction,
          TSoftObjectPtr<UTexture2D> &FactionEmblem, const TCHAR *ParticipantRole) {
        if (!Participant) {
          return;
        }

        const int32 ResolvedId = ResolveStableOrPlayerId(Participant);
        if (ResolvedId > 0 && ResolvedId != PlayerId) {
          UE_LOG(LogSkald, Verbose,
                 TEXT("%s: remapping %s PlayerID from %d to %d using %s"),
                 Context ? Context : TEXT("BeginReadyPhase"), ParticipantRole,
                 PlayerId, ResolvedId, *Participant->GetName());
          PlayerId = ResolvedId;
        }

        bIsAI = Participant->bIsAI;

        if (DisplayName.IsEmpty()) {
          DisplayName = Participant->GetResolvedPlayerName(ParticipantRole);
        }

        if (Faction == ESkaldFaction::None) {
          Faction = Participant->Faction;
        }

        if (GameInstance && !FactionEmblem.ToSoftObjectPath().IsValid() &&
            Faction != ESkaldFaction::None) {
          TSoftObjectPtr<UTexture2D> Emblem =
              GameInstance->GetFactionEmblem(Faction);
          if (Emblem.ToSoftObjectPath().IsValid()) {
            FactionEmblem = Emblem;
          }
        }
      };

  ApplyParticipantDetails(AttackerState, NormalizedBattle.AttackerPlayerID,
                          NormalizedBattle.bAttackerIsAI,
                          NormalizedBattle.AttackerDisplayName,
                          NormalizedBattle.AttackerFaction,
                          NormalizedBattle.AttackerFactionEmblem,
                          TEXT("Attacker"));

  ApplyParticipantDetails(DefenderState, NormalizedBattle.DefenderPlayerID,
                          NormalizedBattle.bDefenderIsAI,
                          NormalizedBattle.DefenderDisplayName,
                          NormalizedBattle.DefenderFaction,
                          NormalizedBattle.DefenderFactionEmblem,
                          TEXT("Defender"));

  LogParticipantResolution(TEXT("Attacker"), AttackerState,
                           NormalizedBattle.AttackerPlayerID);
  LogParticipantResolution(TEXT("Defender"), DefenderState,
                           NormalizedBattle.DefenderPlayerID);

  auto ResolveParticipantId = [&](ASkaldPlayerState *Participant,
                                  int32 PlayerId) {
    const int32 StableId = ResolveStableOrPlayerId(Participant);
    return StableId > 0 ? StableId : PlayerId;
  };

  NormalizedBattle.AttackerPlayerID =
      ResolveParticipantId(AttackerState, NormalizedBattle.AttackerPlayerID);
  NormalizedBattle.DefenderPlayerID =
      ResolveParticipantId(DefenderState, NormalizedBattle.DefenderPlayerID);

  if (GameState) {
    GameState->SetActiveBattlePayload(NormalizedBattle);
  }

  CacheBattleParticipants(NormalizedBattle);

  UE_LOG(LogSkaldReady, Verbose,
         TEXT("%s: caching battle participants for ready state (Attacker=%d AI=%s, Defender=%d AI=%s)"),
         Context ? Context : TEXT("BeginReadyPhase"),
         NormalizedBattle.AttackerPlayerID,
         NormalizedBattle.bAttackerIsAI ? TEXT("true") : TEXT("false"),
         NormalizedBattle.DefenderPlayerID,
         NormalizedBattle.bDefenderIsAI ? TEXT("true") : TEXT("false"));

  PendingBattlePreparation = NormalizedBattle;
  const TCHAR *ResolvedContext =
      Context ? Context : TEXT("BeginReadyPhase");

  PendingBattleReadyState = FSkaldBattleReadyState();
  PendingBattleReadyState.AttackerPlayerID =
      NormalizedBattle.AttackerPlayerID;
  PendingBattleReadyState.DefenderPlayerID =
      NormalizedBattle.DefenderPlayerID;
  PendingBattleReadyState.bAttackerIsAI = NormalizedBattle.bAttackerIsAI;
  PendingBattleReadyState.bDefenderIsAI = NormalizedBattle.bDefenderIsAI;
  PendingBattleReadyState.bAttackerReady =
      PendingBattleReadyState.AttackerPlayerID == INDEX_NONE;
  PendingBattleReadyState.bDefenderReady =
      PendingBattleReadyState.DefenderPlayerID == INDEX_NONE;

  const bool bShouldAutoReadyAttackerAI =
      PendingBattleReadyState.bAttackerIsAI &&
      PendingBattleReadyState.AttackerPlayerID != INDEX_NONE &&
      !PendingBattleReadyState.bAttackerReady;

  if (bShouldAutoReadyAttackerAI) {
    UE_LOG(LogSkaldReady, Verbose,
           TEXT("%s: auto-confirming readiness for AI attacker (PlayerID=%d)."),
           ResolvedContext, PendingBattleReadyState.AttackerPlayerID);
    PendingBattleReadyState.bAttackerReady = true;
  }

  if (PendingBattleReadyState.bAttackerIsAI &&
      PendingBattleReadyState.AttackerPlayerID != INDEX_NONE &&
      !PendingBattleReadyState.bAttackerReady) {
    UE_LOG(LogSkaldReady, Verbose,
           TEXT("%s: attacker AI waiting for player confirmation."),
           ResolvedContext);
  }

  if (PendingBattleReadyState.bDefenderIsAI &&
      PendingBattleReadyState.DefenderPlayerID != INDEX_NONE &&
      !PendingBattleReadyState.bDefenderReady) {
    UE_LOG(LogSkaldReady, Verbose,
           TEXT("%s: defender AI waiting for player confirmation."),
           ResolvedContext);
  }

  TryAutoReadyAI(ResolvedContext);

  UE_LOG(LogSkaldReady, Log,
         TEXT("BeginReadyPhase Attacker=%d Defender=%d From=%d To=%d AI(A=%s,D=%s)"),
         PendingBattleReadyState.AttackerPlayerID,
         PendingBattleReadyState.DefenderPlayerID,
         PendingBattlePreparation.FromTerritoryID,
         PendingBattlePreparation.TargetTerritoryID,
         PendingBattleReadyState.bAttackerIsAI ? TEXT("true") : TEXT("false"),
         PendingBattleReadyState.bDefenderIsAI ? TEXT("true") : TEXT("false"));

  CommitPendingBattleReadyState(ResolvedContext);
  TryAdvanceFromReadyToBattle(ResolvedContext);
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
        SavedPlayerId = PS->GetStablePlayerId();
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

    const bool bIsCapitalAttack = SeededBattle.IsCapitalAttack;
    // Battle maps are always loaded via travel instead of streaming sub-levels.
    bool bShouldStreamSelectedMap = false;
    TSoftObjectPtr<UWorld> SelectedBattleMap;
    if (bIsCapitalAttack && CapitalMaps.Num() > 0) {
      const int32 Index = FMath::RandRange(0, CapitalMaps.Num() - 1);
      SelectedBattleMap = CapitalMaps[Index];
      UE_LOG(LogSkald, Verbose,
             TEXT("TriggerGridBattle: capital attack selecting map '%s'."),
             *SelectedBattleMap.ToString());
    } else if (BattleMapEntries.Num() > 0) {
      const int32 Index = FMath::RandRange(0, BattleMapEntries.Num() - 1);
      const FBattleMapDescriptor &Entry = BattleMapEntries[Index];
      SelectedBattleMap = Entry.Map;
    } else if (BattleMaps.Num() > 0) {
      const int32 Index = FMath::RandRange(0, BattleMaps.Num() - 1);
      SelectedBattleMap = BattleMaps[Index];
    } else if (bIsCapitalAttack) {
      UE_LOG(LogSkald, Warning,
             TEXT("TriggerGridBattle: capital attack has no CapitalMaps configured; falling back to default map."));
    }
    if (SelectedBattleMap.IsNull()) {
      SelectedBattleMap = TSoftObjectPtr<UWorld>(
          FSoftObjectPath(TEXT("/Game/Blueprints/Maps/BattleMap.BattleMap")));
    }

    // Streaming sub-levels is unsupported for battle maps because we always
    // travel to the selected map. Preserve the logging from the previous logic
    // to aid future debugging if streaming is reintroduced.
    const ENetMode NetMode = World->GetNetMode();
    if (bShouldStreamSelectedMap) {
      UE_LOG(LogSkald, Log,
             TEXT("TriggerGridBattle: streaming battle maps is disabled; travelling instead (net mode %d)."),
             static_cast<int32>(NetMode));
      bShouldStreamSelectedMap = false;
    }

    FString MapToLoad =
        SelectedBattleMap.ToSoftObjectPath().GetLongPackageName();
    if (MapToLoad.IsEmpty()) {
      MapToLoad = TEXT("/Game/Blueprints/Maps/BattleMap");
    }

    ASkaldGameState *GS = World->GetGameState<ASkaldGameState>();

    FSkaldTravelState TravelState;
    int32 ValidHumanControllers = 0;
    for (const TWeakObjectPtr<ASkaldPlayerController> &Ptr : Controllers) {
      if (!Ptr.IsValid()) {
        continue;
      }

      const ASkaldPlayerState *ControllerPS =
          Ptr->GetPlayerState<ASkaldPlayerState>();
      if (!ControllerPS || !ControllerPS->bIsAI) {
        ++ValidHumanControllers;
      }
    }
    TravelState.ExpectedControllers = FMath::Max(1, ValidHumanControllers);
    TravelState.AttackerTerritory = SeededBattle.FromTerritoryID;
    TravelState.DefenderTerritory = SeededBattle.TargetTerritoryID;
    TravelState.AttackerPlayerId = SeededBattle.AttackerPlayerID;
    TravelState.DefenderPlayerId = SeededBattle.DefenderPlayerID;
    TravelState.AttackerArmyBudget = SeededBattle.ArmyCountSent;
    TravelState.DefenderArmyBudget = SeededBattle.DefenderArmyCount;
    if (TravelState.DefenderArmyBudget <= 0) {
      TravelState.DefenderArmyBudget =
          SeededBattle.DefenderArmyCount > 0 ? SeededBattle.DefenderArmyCount
                                             : SeededBattle.ArmyCountSent;
    }
    // Ensure the travel state reuses the canonical (or fallback) return map we
    // already resolved for the pending battle payload so clients always know
    // where to go back after combat.
    TravelState.ReturnMap = SeededBattle.ReturnMap;

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

    auto ResolveSnapshotPlayerId = [](ASkaldPlayerState *PS) {
      if (!PS) {
        return 0;
      }

      const int32 StableId = PS->GetStablePlayerId();
      if (StableId > 0) {
        return StableId;
      }

      const int32 AuthoritativeId = PS->GetAuthoritativePlayerId();
      if (AuthoritativeId > 0) {
        return AuthoritativeId;
      }

      return PS->GetPlayerId();
    };

    auto ResolveControllerIndex = [&](ASkaldPlayerState *State) -> int32 {
      if (!State) {
        return INDEX_NONE;
      }
      if (ASkaldPlayerController *Owner = Cast<ASkaldPlayerController>(State->GetOwner())) {
        return Controllers.IndexOfByPredicate(
            [Owner](const TWeakObjectPtr<ASkaldPlayerController> &Ptr) {
              return Ptr.Get() == Owner;
            });
      }
      return INDEX_NONE;
    };

    if (TravelState.PlayerSnapshots.Num() == 0 && GS) {
      TravelState.PlayerSnapshots.Reserve(GS->PlayerArray.Num());
      for (APlayerState *BasePS : GS->PlayerArray) {
        if (ASkaldPlayerState *SkaldPS = Cast<ASkaldPlayerState>(BasePS)) {
          FS_PlayerData Snapshot;
          Snapshot.PlayerID = ResolveSnapshotPlayerId(SkaldPS);
          Snapshot.PlayerName = SkaldPS->GetResolvedPlayerName(TEXT("TravelState"));
          Snapshot.DisplayName = SkaldPS->PlayerDisplayName;
          Snapshot.DesiredControllerIndex = ResolveControllerIndex(SkaldPS);
          Snapshot.DesiredTurnIndex = Snapshot.DesiredControllerIndex;
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

          const int32 PlayerId = ResolveSnapshotPlayerId(SkaldPS);
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
        SnapshotPtr->DisplayName = SkaldPS->PlayerDisplayName;
        SnapshotPtr->DesiredControllerIndex = ResolveControllerIndex(SkaldPS);
        SnapshotPtr->DesiredTurnIndex = SnapshotPtr->DesiredControllerIndex;
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

    auto ResolveParticipant =
        [&](int32 TerritoryId, ASkaldPlayerState *ExistingPS, int32 &OutPlayerID,
            FString &OutName, ESkaldFaction &OutFaction, bool &bOutIsAI,
            TSoftObjectPtr<UTexture2D> &OutEmblem) -> ASkaldPlayerState * {
      ASkaldPlayerState *Resolved = ExistingPS;
      if (!Resolved) {
        if (ATerritory *Territory = FindTerritory(TerritoryId)) {
          Resolved = Territory->OwningPlayer;
        }
      }
      if (!Resolved && GS && OutPlayerID > 0) {
        Resolved = GS->GetPlayerByStableId(OutPlayerID);
        if (!Resolved) {
          Resolved = GS->GetPlayerById(OutPlayerID);
        }
      }
      if (Resolved) {
        OutPlayerID = ResolveSnapshotPlayerId(Resolved);
        OutName = Resolved->GetResolvedPlayerName(TEXT("TriggerGridBattle"));
        OutFaction = Resolved->Faction;
        bOutIsAI = Resolved->bIsAI;
        if (!OutEmblem.ToSoftObjectPath().IsValid() &&
            OutFaction != ESkaldFaction::None) {
          if (USkaldGameInstance *LocalGI =
                  GetGameInstance<USkaldGameInstance>()) {
            OutEmblem = LocalGI->GetFactionEmblem(OutFaction);
          }
        }
      }
      return Resolved;
    };

    ResolveParticipant(PendingPayload.FromTerritoryID, nullptr,
                      PendingPayload.AttackerPlayerID,
                      PendingPayload.AttackerDisplayName,
                      PendingPayload.AttackerFaction,
                      PendingPayload.bAttackerIsAI,
                      PendingPayload.AttackerFactionEmblem);
    ResolveParticipant(PendingPayload.TargetTerritoryID, nullptr,
                      PendingPayload.DefenderPlayerID,
                      PendingPayload.DefenderDisplayName,
                      PendingPayload.DefenderFaction,
                      PendingPayload.bDefenderIsAI,
                      PendingPayload.DefenderFactionEmblem);

    ATerritory *AttackTerritory =
        FindTerritory(PendingPayload.FromTerritoryID);
    ATerritory *DefTerritory = FindTerritory(PendingPayload.TargetTerritoryID);
    if (PendingPayload.AttackerTerritoryName.IsEmpty() && AttackTerritory) {
      PendingPayload.AttackerTerritoryName = AttackTerritory->TerritoryName;
    }
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
      TravelState.TravelSessionToken = GCurrentTravelSessionToken;
      uint32 PayloadHash = 0; PayloadHash = HashCombine(PayloadHash, ::GetTypeHash(PendingPayload.FromTerritoryID)); PayloadHash = HashCombine(PayloadHash, ::GetTypeHash(PendingPayload.TargetTerritoryID));
      UE_LOG(LogSkald, Log, TEXT("[TravelToken] Token=%s Stage=TriggerGridBattle World=%s Ctx=TriggerGridBattle PayloadValid=1 Hash=%u"), *GCurrentTravelSessionToken, *GetNameSafe(World), PayloadHash);
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

    if (bShouldStreamSelectedMap && !bStreamingBattle) {
      UE_LOG(LogSkald, Warning,
             TEXT("TriggerGridBattle: battle level streaming failed for %s; scheduling retry instead of map travel."),
             *SelectedBattleMap.ToSoftObjectPath().ToString());
      DeferredPendingBattle = PendingPayload;
      if (World &&
          !World->GetTimerManager().IsTimerActive(PendingBattleTravelRetryHandle)) {
        FTimerDelegate RetryDelegate = FTimerDelegate::CreateUObject(
            this, &ATurnManager::RetryPendingBattleTravel);
        constexpr float RetryDelaySeconds = 0.15f;
        World->GetTimerManager().SetTimer(PendingBattleTravelRetryHandle,
                                          RetryDelegate, RetryDelaySeconds, false);
      }
    } else if (!bShouldStreamSelectedMap) {
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

bool ATurnManager::TryAutoReadyAI(const TCHAR *Context) {
  const TCHAR *ResolvedContext = Context ? Context : TEXT("TryAutoReadyAI");

  const bool bHumanPending =
      (!PendingBattleReadyState.bAttackerIsAI &&
       PendingBattleReadyState.AttackerPlayerID != INDEX_NONE &&
       !PendingBattleReadyState.bAttackerReady) ||
      (!PendingBattleReadyState.bDefenderIsAI &&
       PendingBattleReadyState.DefenderPlayerID != INDEX_NONE &&
       !PendingBattleReadyState.bDefenderReady);

  if (bHumanPending) {
    UE_LOG(LogSkaldReady, Verbose,
           TEXT("%s: AI auto-ready deferred while waiting for player confirmation."),
           ResolvedContext);
    return false;
  }

  bool bChanged = false;
  const TArray<ASkaldPlayerController *> ControllerSnapshot = GetControllers();
  const auto HasControllerForParticipant =
      [&ControllerSnapshot](int32 ParticipantId) {
        if (ParticipantId == INDEX_NONE) {
          return false;
        }

        for (ASkaldPlayerController *Controller : ControllerSnapshot) {
          if (!Controller) {
            continue;
          }

          if (ASkaldPlayerState *PS =
                  Controller->GetPlayerState<ASkaldPlayerState>()) {
            if (PS->GetPlayerId() == ParticipantId) {
              return true;
            }
          }
        }

        return false;
      };

  const auto AutoReadyParticipant = [&](int32 ParticipantId,
                                        bool bParticipantIsAI,
                                        bool &bParticipantReady,
                                        const TCHAR *ParticipantLabel) {
    if (!bParticipantIsAI || ParticipantId == INDEX_NONE || bParticipantReady) {
      return false;
    }

    if (HasControllerForParticipant(ParticipantId)) {
      UE_LOG(LogSkaldReady, Verbose,
             TEXT("%s: awaiting decision from AI %s (PlayerID=%d)."),
             ResolvedContext, ParticipantLabel, ParticipantId);
      return false;
    }

    UE_LOG(LogSkaldReady, Log,
           TEXT("%s: auto-readying AI %s (PlayerID=%d) due to missing controller."),
           ResolvedContext, ParticipantLabel, ParticipantId);
    bParticipantReady = true;
    return true;
  };

  bChanged |= AutoReadyParticipant(
      PendingBattleReadyState.AttackerPlayerID,
      PendingBattleReadyState.bAttackerIsAI,
      PendingBattleReadyState.bAttackerReady, TEXT("attacker"));

  bChanged |= AutoReadyParticipant(
      PendingBattleReadyState.DefenderPlayerID,
      PendingBattleReadyState.bDefenderIsAI,
      PendingBattleReadyState.bDefenderReady, TEXT("defender"));

  if (!bChanged) {
    const bool bHasAIParticipant =
        (PendingBattleReadyState.bAttackerIsAI &&
         PendingBattleReadyState.AttackerPlayerID != INDEX_NONE) ||
        (PendingBattleReadyState.bDefenderIsAI &&
         PendingBattleReadyState.DefenderPlayerID != INDEX_NONE);

    if (bHasAIParticipant) {
      UE_LOG(LogSkaldReady, Verbose,
             TEXT("%s: AI participants already awaiting confirmation."),
             ResolvedContext);
    }

    return false;
  }

  return true;
}

void ATurnManager::NotifyPlayerReadyForBattle(int32 PlayerID, bool bReady) {
  const bool bHasPendingBattle =
      PendingBattlePreparation.FromTerritoryID != 0 ||
      PendingBattlePreparation.TargetTerritoryID != 0;
  if (!bHasPendingBattle) {
    UE_LOG(LogSkaldReady, Verbose,
           TEXT("NotifyPlayerReadyForBattle ignored: no pending battle."));
    return;
  }

  ASkaldGameState *GameState = GetWorld()
                                   ? GetWorld()->GetGameState<ASkaldGameState>()
                                   : nullptr;
  if (GameState) {
      ASkaldPlayerState *ResolvedPS =
          GameState->GetPlayerByStableId(PlayerID);
      if (!ResolvedPS) {
        ResolvedPS = GameState->GetPlayerById(PlayerID);
      }

      if (ResolvedPS) {
        PlayerID = ResolveStableOrPlayerId(ResolvedPS);
      }
    }

  const auto ApplyReadyState = [&](int32 ParticipantId, bool bParticipantIsAI,
                                   bool &bParticipantReady,
                                   const TCHAR *ParticipantLabel) {
    if (ParticipantId != PlayerID) {
      return false;
    }

    const bool bEffectiveReady = bReady || ParticipantId == INDEX_NONE;
    if (bParticipantReady == bEffectiveReady) {
      UE_LOG(LogSkaldReady, Verbose,
             TEXT("SetReady unchanged PlayerID=%d Role=%s Ready=%s AI=%s Requested=%s"),
             PlayerID, ParticipantLabel,
             bParticipantReady ? TEXT("true") : TEXT("false"),
             bParticipantIsAI ? TEXT("true") : TEXT("false"),
             bReady ? TEXT("true") : TEXT("false"));
      return false;
    }

    UE_LOG(LogSkaldReady, Log,
           TEXT("SetReady PlayerID=%d Role=%s Ready=%s AI=%s Requested=%s"),
           PlayerID, ParticipantLabel, bEffectiveReady ? TEXT("true") : TEXT("false"),
           bParticipantIsAI ? TEXT("true") : TEXT("false"),
           bReady ? TEXT("true") : TEXT("false"));
    bParticipantReady = bEffectiveReady;
    return true;
  };

  const bool bAttackerChanged = ApplyReadyState(
      PendingBattleReadyState.AttackerPlayerID,
      PendingBattleReadyState.bAttackerIsAI,
      PendingBattleReadyState.bAttackerReady, TEXT("attacker"));

  const bool bDefenderChanged = ApplyReadyState(
      PendingBattleReadyState.DefenderPlayerID,
      PendingBattleReadyState.bDefenderIsAI,
      PendingBattleReadyState.bDefenderReady, TEXT("defender"));

  auto ResetAIReadiness = [&](bool bChangedParticipantReady,
                              bool bOtherParticipantIsAI, bool &bOtherParticipantReady,
                              const TCHAR *OtherLabel) {
    if (bChangedParticipantReady || !bOtherParticipantIsAI ||
        !bOtherParticipantReady) {
      return false;
    }

    UE_LOG(LogSkaldReady, Log,
           TEXT("NotifyPlayerReadyForBattle: clearing AI %s readiness until player reconfirms."),
           OtherLabel);
    bOtherParticipantReady = false;
    return true;
  };

  if (bAttackerChanged && !PendingBattleReadyState.bAttackerIsAI) {
    ResetAIReadiness(PendingBattleReadyState.bAttackerReady,
                     PendingBattleReadyState.bDefenderIsAI,
                     PendingBattleReadyState.bDefenderReady, TEXT("defender"));
  }

  if (bDefenderChanged && !PendingBattleReadyState.bDefenderIsAI) {
    ResetAIReadiness(PendingBattleReadyState.bDefenderReady,
                     PendingBattleReadyState.bAttackerIsAI,
                     PendingBattleReadyState.bAttackerReady, TEXT("attacker"));
  }

  if (!bAttackerChanged && !bDefenderChanged) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("NotifyPlayerReadyForBattle: PlayerID %d is not part of the pending battle."),
           PlayerID);
    return;
  }

  TryAutoReadyAI(TEXT("NotifyPlayerReadyForBattle"));

  CommitPendingBattleReadyState(TEXT("NotifyPlayerReadyForBattle"));
  TryAdvanceFromReadyToBattle(TEXT("NotifyPlayerReadyForBattle"));
}

void ATurnManager::BroadcastPrepareForBattlePrompt(
    const FS_BattlePayload &Battle, const TCHAR *LogContext) {
  const bool bHasAuthority = HasAuthority();
  const TCHAR *ResolvedLogContext =
      LogContext ? LogContext : TEXT("BroadcastPrepareForBattlePrompt");

  const bool bNeedsAttackerConfirmation =
      !PendingBattleReadyState.bAttackerReady &&
      PendingBattleReadyState.AttackerPlayerID != INDEX_NONE;
  const bool bNeedsDefenderConfirmation =
      !PendingBattleReadyState.bDefenderReady &&
      PendingBattleReadyState.DefenderPlayerID != INDEX_NONE;

  const bool bHasPayload = Battle.FromTerritoryID != 0 ||
                           Battle.TargetTerritoryID != 0;

  const TArray<ASkaldPlayerController *> ControllerSnapshot = GetControllers();
  auto HidePromptForController = [](ASkaldPlayerController *Controller) {
    if (!Controller) {
      return;
    }

    const ENetMode NetMode = Controller->GetNetMode();
    if (NetMode == NM_Standalone) {
      Controller->HidePrepareForBattlePromptLocal();
    } else {
      Controller->ClientHidePrepareForBattle();
    }
  };

  auto HideAllControllers = [&ControllerSnapshot, &HidePromptForController]() {
    for (ASkaldPlayerController *Controller : ControllerSnapshot) {
      HidePromptForController(Controller);
    }
  };

  if (!bHasPayload) {
    if (bNeedsAttackerConfirmation || bNeedsDefenderConfirmation) {
      UE_LOG(LogSkald, Warning,
             TEXT("%s: pending battle readiness exists but cached payload is empty."),
             LogContext);
    }

    HideAllControllers();
    return;
  }

  if (!bNeedsAttackerConfirmation && !bNeedsDefenderConfirmation) {
    HideAllControllers();
    return;
  }

  auto ShouldDeferForOpponentAIDecision =
      [&](ASkaldPlayerController *Participant, bool bParticipantIsAttacker) {
        if (!bHasAuthority || !Participant) {
          return false;
        }

        const bool bOpponentIsAI = bParticipantIsAttacker
                                        ? PendingBattleReadyState.bDefenderIsAI
                                        : PendingBattleReadyState.bAttackerIsAI;

        if (!bOpponentIsAI) {
          return false;
        }

        const int32 OpponentPlayerId = bParticipantIsAttacker
                                           ? PendingBattleReadyState.DefenderPlayerID
                                           : PendingBattleReadyState.AttackerPlayerID;

        if (OpponentPlayerId == INDEX_NONE) {
          return false;
        }

        const bool bOpponentReady = bParticipantIsAttacker
                                        ? PendingBattleReadyState.bDefenderReady
                                        : PendingBattleReadyState.bAttackerReady;

        if (bOpponentReady) {
          return false;
        }

        const TCHAR *WaitingOnRole = bParticipantIsAttacker ? TEXT("defender")
                                                            : TEXT("attacker");
        UE_LOG(LogSkaldReady, Verbose,
               TEXT("%s: deferring prepare prompt for %s while AI %s resolves decision."),
               ResolvedLogContext, *GetNameSafe(Participant), WaitingOnRole);
        return true;
      };

  bool bAttackerDeferred = false;
  bool bDefenderDeferred = false;

  UWorld *World = GetWorld();
  ASkaldGameState *GameState =
      World ? World->GetGameState<ASkaldGameState>() : nullptr;
  AWorldMap *WorldMap = ResolveWorldMap();
  USkaldGameInstance *GameInstance = GetGameInstance<USkaldGameInstance>();

  auto ResolvePlayerName = [&](int32 PlayerId, const FString &CachedName,
                               const TCHAR *Context) -> FText {
    if (!CachedName.IsEmpty()) {
      return FText::FromString(CachedName);
    }

    if (GameState && PlayerId > 0) {
      if (ASkaldPlayerState *PlayerState = GameState->GetPlayerById(PlayerId)) {
        const FString Resolved =
            PlayerState->GetResolvedPlayerName(Context);
        if (!Resolved.IsEmpty()) {
          return FText::FromString(Resolved);
        }
      }
    }

    if (PlayerId > 0) {
      UE_LOG(LogSkald, Verbose,
             TEXT("%s: using fallback name for %s (PlayerID %d)"), LogContext,
             Context, PlayerId);
      return FText::Format(
          NSLOCTEXT("SkaldHUD", "Prepare_PlayerIDFallback", "Player #{0}"),
          FText::AsNumber(PlayerId));
    }

    UE_LOG(LogSkald, Warning,
           TEXT("%s: missing player name for %s"), LogContext, Context);
    return NSLOCTEXT("SkaldHUD", "Prepare_UnknownPlayer", "Unknown Player");
  };

  auto ResolveTerritoryName = [&](int32 TerritoryId, const FString &CachedName,
                                  const TCHAR *Context) -> FText {
    if (!CachedName.IsEmpty()) {
      return FText::FromString(CachedName);
    }

    if (WorldMap && TerritoryId != 0) {
      if (ATerritory *Territory = WorldMap->GetTerritoryById(TerritoryId)) {
        if (Territory && !Territory->TerritoryName.IsEmpty()) {
          return FText::FromString(Territory->TerritoryName);
        }
      }
    }

    if (TerritoryId != 0) {
      UE_LOG(LogSkald, Verbose,
             TEXT("%s: using fallback territory ID for %s (ID %d)"),
             LogContext, Context, TerritoryId);
      return FText::Format(NSLOCTEXT("SkaldHUD", "Prepare_TerritoryIDFallback",
                                     "Territory #{0}"),
                           FText::AsNumber(TerritoryId));
    }

    UE_LOG(LogSkald, Warning,
           TEXT("%s: missing territory data for %s"), LogContext, Context);
    return NSLOCTEXT("SkaldHUD", "Prepare_UnknownTerritory",
                     "Unknown Territory");
  };

  auto ResolveFactionEmblem =
      [&](const TSoftObjectPtr<UTexture2D> &Source, ESkaldFaction Faction,
          const TCHAR *Context) -> TSoftObjectPtr<UTexture2D> {
        if (Source.ToSoftObjectPath().IsValid()) {
          return Source;
        }

        if (Faction != ESkaldFaction::None && GameInstance) {
          TSoftObjectPtr<UTexture2D> Emblem =
              GameInstance->GetFactionEmblem(Faction);
          if (Emblem.ToSoftObjectPath().IsValid()) {
            return Emblem;
          }
        }

        if (Faction != ESkaldFaction::None) {
          UE_LOG(LogSkald, Verbose,
                 TEXT("%s: missing faction emblem for %s (%s)"), LogContext,
                 Context,
                 *StaticEnum<ESkaldFaction>()->GetNameStringByValue(
                     static_cast<int64>(Faction)));
        }

        return TSoftObjectPtr<UTexture2D>();
      };

  FPrepareForBattlePromptData PromptData;
  PromptData.AttackerPlayerID = Battle.AttackerPlayerID;
  PromptData.DefenderPlayerID = Battle.DefenderPlayerID;
  PromptData.AttackingTerritoryID = Battle.FromTerritoryID;
  PromptData.DefendingTerritoryID = Battle.TargetTerritoryID;
  PromptData.AttackerFaction = Battle.AttackerFaction;
  PromptData.DefenderFaction = Battle.DefenderFaction;
  PromptData.AttackerDisplayName = ResolvePlayerName(
      Battle.AttackerPlayerID, Battle.AttackerDisplayName, TEXT("Attacker"));
  PromptData.DefenderDisplayName = ResolvePlayerName(
      Battle.DefenderPlayerID, Battle.DefenderDisplayName, TEXT("Defender"));
  PromptData.AttackingTerritoryName = ResolveTerritoryName(
      Battle.FromTerritoryID, Battle.AttackerTerritoryName,
      TEXT("AttackingTerritory"));
  PromptData.DefendingTerritoryName = ResolveTerritoryName(
      Battle.TargetTerritoryID, Battle.DefenderTerritoryName,
      TEXT("DefendingTerritory"));
  PromptData.AttackerCommittedArmy =
      FMath::Max(0, Battle.ArmyCountSent);

  int32 ResolvedDefenderArmyCount = Battle.DefenderArmyCount;
  if (ResolvedDefenderArmyCount <= 0 && WorldMap) {
    if (ATerritory *DefendingTerritory =
            WorldMap->GetTerritoryById(Battle.TargetTerritoryID)) {
      ResolvedDefenderArmyCount = DefendingTerritory->ArmyUnits;
    }
  }
  PromptData.DefenderArmyCount = FMath::Max(0, ResolvedDefenderArmyCount);
  PromptData.AttackerFactionEmblem =
      ResolveFactionEmblem(Battle.AttackerFactionEmblem, Battle.AttackerFaction,
                           TEXT("Attacker"));
  PromptData.DefenderFactionEmblem =
      ResolveFactionEmblem(Battle.DefenderFactionEmblem, Battle.DefenderFaction,
                           TEXT("Defender"));

  bool bAttackerMatched = !bNeedsAttackerConfirmation;
  bool bDefenderMatched = !bNeedsDefenderConfirmation;

  const FString ExpectedAttackerName = PromptData.AttackerDisplayName.ToString();
  const FString ExpectedDefenderName = PromptData.DefenderDisplayName.ToString();

  auto ResolveParticipantStateByContext = [&](bool bIsAttacker) {
    const int32 PlayerId = bIsAttacker
                               ? PendingBattleReadyState.AttackerPlayerID
                               : PendingBattleReadyState.DefenderPlayerID;

    if (PlayerId != INDEX_NONE && GameState) {
      if (ASkaldPlayerState *Resolved = GameState->GetPlayerById(PlayerId)) {
        return Resolved;
      }
    }

    if (WorldMap) {
      const int32 TerritoryId =
          bIsAttacker ? Battle.FromTerritoryID : Battle.TargetTerritoryID;
      if (ATerritory *Territory = WorldMap->GetTerritoryById(TerritoryId)) {
        if (ASkaldPlayerState *Owner = Territory->OwningPlayer) {
          return Owner;
        }
      }
    }

    return static_cast<ASkaldPlayerState *>(nullptr);
  };

  ASkaldPlayerState *ExpectedAttacker = ResolveParticipantStateByContext(true);
  ASkaldPlayerState *ExpectedDefender = ResolveParticipantStateByContext(false);

  auto MatchesParticipant = [&](ASkaldPlayerState *Candidate,
                                ASkaldPlayerState *Expected, int32 ExpectedId,
                                const FString &ExpectedName) {
    if (!Candidate) {
      return false;
    }

    if (Expected && Candidate == Expected) {
      return true;
    }

    if (ExpectedId != INDEX_NONE && Candidate->GetPlayerId() == ExpectedId) {
      return true;
    }

    if (ExpectedName.IsEmpty()) {
      return false;
    }

    FString CandidateName = Candidate->PlayerDisplayName;
    if (CandidateName.IsEmpty()) {
      CandidateName = Candidate->GetResolvedPlayerName(
          TEXT("BroadcastPrepareForBattlePrompt_Match"));
    }

    return !CandidateName.IsEmpty() &&
           CandidateName.Equals(ExpectedName, ESearchCase::IgnoreCase);
  };

  for (ASkaldPlayerController *Controller : ControllerSnapshot) {
    if (!Controller) {
      continue;
    }

    ASkaldPlayerState *PS = Controller->GetPlayerState<ASkaldPlayerState>();
    if (!PS) {
      UE_LOG(LogSkald, Verbose,
             TEXT("%s: controller %s has no PlayerState when evaluating prepare-for-battle prompt."),
             LogContext, *Controller->GetName());
    }

    const bool bMatchesAttackerId = MatchesParticipant(
        PS, ExpectedAttacker, PendingBattleReadyState.AttackerPlayerID,
        ExpectedAttackerName);
    const bool bMatchesDefenderId = MatchesParticipant(
        PS, ExpectedDefender, PendingBattleReadyState.DefenderPlayerID,
        ExpectedDefenderName);

    const bool bIsAttacker = bNeedsAttackerConfirmation && bMatchesAttackerId;
    const bool bIsDefender = bNeedsDefenderConfirmation && bMatchesDefenderId;

    if (bIsAttacker || bIsDefender) {
      const bool bParticipantIsAI = PS && PS->bIsAI;
      const TCHAR *RoleLabel = bIsAttacker ? TEXT("Attacker") : TEXT("Defender");

      if (bParticipantIsAI) {
        UE_LOG(LogSkaldReady, Log,
               TEXT("%s: skipping prepare prompt for AI-controlled %s (Controller=%s PlayerID=%d)."),
               LogContext, RoleLabel, *Controller->GetName(),
               PS ? PS->GetPlayerId() : INDEX_NONE);

        if (ASkaldAIController *AIController =
                Cast<ASkaldAIController>(Controller)) {
          AIController->HandlePrepareForBattlePromptDirect(PromptData);
        } else {
          UE_LOG(LogSkaldReady, Warning,
                 TEXT("%s: participant flagged as AI but controller %s is not ASkaldAIController."),
                 LogContext, *Controller->GetName());
        }
      } else if (ShouldDeferForOpponentAIDecision(Controller, bIsAttacker)) {
        if (bIsAttacker) {
          bAttackerDeferred = true;
        }
        if (bIsDefender) {
          bDefenderDeferred = true;
        }
        continue;
      } else {
        UE_LOG(LogSkaldReady, Log,
               TEXT("WidgetSpawned PC=%s Role=%s Battle=%d->%d"),
               *Controller->GetName(), RoleLabel,
               PendingBattlePreparation.FromTerritoryID,
               PendingBattlePreparation.TargetTerritoryID);

        const ENetMode NetMode = Controller->GetNetMode();
        if (NetMode == NM_Standalone) {
          Controller->ShowPrepareForBattlePromptLocal(PromptData);
        } else {
          Controller->ClientShowPrepareForBattle(PromptData);
        }
      }

      if (bIsAttacker) {
        bAttackerMatched = true;
      }
      if (bIsDefender) {
        bDefenderMatched = true;
      }
      continue;
    }

    HidePromptForController(Controller);

    if (bMatchesAttackerId && !bNeedsAttackerConfirmation) {
      UE_LOG(LogSkald, Log,
             TEXT("%s: cleared attacker prompt for controller %s (PlayerID %d) because confirmation is no longer required."),
             LogContext, *Controller->GetName(),
             PS ? PS->GetPlayerId() : INDEX_NONE);
    } else if (bMatchesDefenderId && !bNeedsDefenderConfirmation) {
      UE_LOG(LogSkald, Log,
             TEXT("%s: cleared defender prompt for controller %s (PlayerID %d) because confirmation is no longer required."),
             LogContext, *Controller->GetName(),
             PS ? PS->GetPlayerId() : INDEX_NONE);
    }
  }

  if (bNeedsAttackerConfirmation && !bAttackerMatched &&
      !bAttackerDeferred) {
    UE_LOG(LogSkald, Warning,
           TEXT("%s: pending attacker PlayerID %d has no registered controller."),
           LogContext, PendingBattleReadyState.AttackerPlayerID);
  }

  if (bNeedsDefenderConfirmation && !bDefenderMatched &&
      !bDefenderDeferred) {
    UE_LOG(LogSkald, Warning,
           TEXT("%s: pending defender PlayerID %d has no registered controller."),
           LogContext, PendingBattleReadyState.DefenderPlayerID);
  }
}

void ATurnManager::CommitPendingBattleReadyState(const TCHAR *Context) {
  if (!HasAuthority()) {
    UE_LOG(LogSkald, Warning,
           TEXT("%s: CommitPendingBattleReadyState called without authority."),
           Context ? Context : TEXT("CommitPendingBattleReadyState"));
    if (UWorld *World = GetWorld()) {
      PendingBattleReadyState.LastUpdatedTimeSeconds = World->GetTimeSeconds();
    } else {
      PendingBattleReadyState.LastUpdatedTimeSeconds = FPlatformTime::Seconds();
    }
    return;
  }

  UWorld *World = GetWorld();
  ASkaldGameState *GameState =
      World ? World->GetGameState<ASkaldGameState>() : nullptr;

  if (GameState) {
    GameState->SetPendingBattleReady(PendingBattleReadyState);
    PendingBattleReadyState = GameState->GetPendingBattleReady();
  } else if (World) {
    PendingBattleReadyState.LastUpdatedTimeSeconds = World->GetTimeSeconds();
  } else {
    PendingBattleReadyState.LastUpdatedTimeSeconds = FPlatformTime::Seconds();
  }

  UE_LOG(LogSkald, Verbose,
         TEXT("%s: committed ready state (Attacker=%d Ready=%s AI=%s, Defender=%d Ready=%s AI=%s, t=%.3f)"),
         Context ? Context : TEXT("CommitPendingBattleReadyState"),
         PendingBattleReadyState.AttackerPlayerID,
         PendingBattleReadyState.bAttackerReady ? TEXT("true") : TEXT("false"),
         PendingBattleReadyState.bAttackerIsAI ? TEXT("true") : TEXT("false"),
         PendingBattleReadyState.DefenderPlayerID,
         PendingBattleReadyState.bDefenderReady ? TEXT("true") : TEXT("false"),
         PendingBattleReadyState.bDefenderIsAI ? TEXT("true") : TEXT("false"),
         PendingBattleReadyState.LastUpdatedTimeSeconds);

  MulticastOnReadyStateChanged(PendingBattleReadyState, PendingBattlePreparation);
}

bool ATurnManager::TryAdvanceFromReadyToBattle(const TCHAR *Context) {
  const bool bHasPendingBattle =
      PendingBattlePreparation.FromTerritoryID != 0 ||
      PendingBattlePreparation.TargetTerritoryID != 0;
  if (!bHasPendingBattle) {
    return false;
  }

  UWorld *World = GetWorld();
  ASkaldGameState *GameState =
      World ? World->GetGameState<ASkaldGameState>() : nullptr;

  bool bReadyToLaunch = false;
  if (GameState) {
    PendingBattleReadyState = GameState->GetPendingBattleReady();
    bReadyToLaunch = GameState->AreAllRequiredPartiesReady();
  } else {
    const bool bAttackerReady = PendingBattleReadyState.bAttackerReady ||
                                PendingBattleReadyState.AttackerPlayerID == INDEX_NONE;
    const bool bDefenderReady = PendingBattleReadyState.bDefenderReady ||
                                PendingBattleReadyState.DefenderPlayerID == INDEX_NONE;
    bReadyToLaunch = bAttackerReady && bDefenderReady;
  }

  if (!bReadyToLaunch) {
    return false;
  }

  for (ASkaldPlayerController *Controller : GetControllers()) {
    if (!Controller) {
      continue;
    }

    const ENetMode NetMode = Controller->GetNetMode();
    if (NetMode == NM_Standalone) {
      Controller->HidePrepareForBattlePromptLocal();
    } else {
      Controller->ClientHidePrepareForBattle();
    }
  }

  FS_BattlePayload BattleToLaunch = PendingBattlePreparation;
  PendingBattlePreparation = FS_BattlePayload();
  PendingBattleReadyState = FSkaldBattleReadyState();
  CommitPendingBattleReadyState(TEXT("TryAdvanceFromReadyToBattle_Clear"));

  UE_LOG(LogSkaldReady, Log,
         TEXT("AllReady=true -> StartBattleTravel Battle=%d->%d (Context=%s)"),
         BattleToLaunch.FromTerritoryID, BattleToLaunch.TargetTerritoryID,
         Context ? Context : TEXT("TryAdvanceFromReadyToBattle"));

  TriggerGridBattle(BattleToLaunch);
  return true;
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

void ATurnManager::MulticastOnReadyStateChanged_Implementation(
    const FSkaldBattleReadyState &ReadyState,
    const FS_BattlePayload &BattlePayload) {
  PendingBattleReadyState = ReadyState;
  PendingBattlePreparation = BattlePayload;

  UE_LOG(LogSkald, Verbose,
         TEXT("MulticastOnReadyStateChanged: Attacker=%d Ready=%s AI=%s, Defender=%d Ready=%s AI=%s, t=%.3f"),
         ReadyState.AttackerPlayerID,
         ReadyState.bAttackerReady ? TEXT("true") : TEXT("false"),
         ReadyState.bAttackerIsAI ? TEXT("true") : TEXT("false"),
         ReadyState.DefenderPlayerID,
         ReadyState.bDefenderReady ? TEXT("true") : TEXT("false"),
         ReadyState.bDefenderIsAI ? TEXT("true") : TEXT("false"),
         ReadyState.LastUpdatedTimeSeconds);

  BroadcastPrepareForBattlePrompt(BattlePayload,
                                  TEXT("MulticastOnReadyStateChanged"));
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

  if (IsCurrentControllerAI()) {
    BeginBattleResultAcknowledgementWindow();
  } else {
    ClearBattleResultAcknowledgements();
  }

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
      Snapshot.OwnerPlayerID = OwnerPS ? ResolveStableOrPlayerId(OwnerPS) : 0;
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
    ResetMovementActionsForActivePlayer();
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
  bool bBlockAdvance = false;
  ASkaldPlayerState *ActivePS = nullptr;

  if (HasPendingBattlePreparation()) {
    UE_LOG(LogSkald, Verbose,
           TEXT("EndCurrentPhase blocked: pending battle preparation awaiting readiness."));
    return;
  }

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
    if (ASkaldGameState *GS = GetWorld()->GetGameState<ASkaldGameState>()) {
      ActivePS = GS->GetPlayerAtTurnIndex(GS->CurrentTurnIndex);
      if (ActivePS && !ActivePS->bIsAI && ActivePS->DeployableUnits > 0) {
        bool bCanPlaceMore = false;
        if (AWorldMap *WorldMap = ResolveWorldMap()) {
          for (ATerritory *Terr : WorldMap->Territories) {
            if (Terr && Terr->OwningPlayer == ActivePS) {
              const int32 TerritoryId = Terr->GetTerritoryId();
              const int32 AlreadyPlaced =
                  ActivePS->GetArmyPlacementDeploymentForTerritory(TerritoryId);
              if (AlreadyPlaced < Skald::ArmyPlacement::DeployPerTerritoryLimit) {
                bCanPlaceMore = true;
                break;
              }
            }
          }
        }

        if (bCanPlaceMore) {
          bBlockAdvance = true;
          UE_LOG(LogSkald, Verbose,
                 TEXT("EndCurrentPhase blocked: Human player %s still has %d units to place."),
                 *ActivePS->GetResolvedPlayerName(TEXT("EndCurrentPhase_ArmyPlacement")),
                 ActivePS->DeployableUnits);
        } else {
          UE_LOG(LogSkald, Verbose,
                 TEXT("EndCurrentPhase: Allowing advance despite %d unplaced units due to per-territory placement cap."),
                 ActivePS->DeployableUnits);
        }
      }
    }

    if (bBlockAdvance) {
      return;
    }

    if (ActivePS && ActivePS->DeployableUnits > 0) {
      if (AWorldMap *WorldMap = ResolveWorldMap()) {
        const int32 Distributed =
            WorldMap->DistributeUnplacedArmyPlacementUnits(ActivePS);
        if (Distributed > 0) {
          BroadcastDeployableUnits(ActivePS);
        }
      }
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
  UE_LOG(LogSkald, Log, TEXT("[PhaseGuard] From=%s To=%s Active=%d Allowed=%d Reason=%s"),
         TEXT("Unknown"), *PhaseString, GetActivePlayerId(), 1, TEXT("BroadcastCurrentPhase"));
  UE_LOG(LogSkald, Log, TEXT("BroadcastCurrentPhase: %s"), *PhaseString);
  if (GEngine) {
    GEngine->AddOnScreenDebugMessage(
        -1, 5.f, FColor::Green,
        FString::Printf(TEXT("Current Phase: %s"), *PhaseString));
  }

  if (ASkaldGameState *GS = GetWorld()->GetGameState<ASkaldGameState>()) {
    for (APlayerState *RawPlayerState : GS->PlayerArray) {
      if (ASkaldPlayerState *SkaldPS = Cast<ASkaldPlayerState>(RawPlayerState)) {
        const int32 StableId = SkaldPS->GetStablePlayerId();
        ATerritory *Selection = SkaldPS->SelectedTerritory.Get();
        const FString SelectionDesc = Selection
                                          ? FString::Printf(TEXT("%s (%d)"),
                                                            *Selection->GetName(),
                                                            Selection->TerritoryID)
                                          : FString(TEXT("None"));
        UE_LOG(LogSkald, Log,
               TEXT("Phase %s selection snapshot for %s (StableId=%d): %s"),
               *PhaseString,
               *SkaldPS->GetResolvedPlayerName(TEXT("BroadcastCurrentPhase")),
               StableId, *SelectionDesc);
      }
    }
  }

  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr :
       Controllers) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      // Mirror the client-side replication path locally so the listen server
      // host refreshes phase UI, turn ownership, and battle HUD without
      // issuing host-driven RPCs. Remote clients will respond via
      // OnRep_CurrentPhase and the replicated GameState/turn state.
      Controller->RefreshTurnDataFromState();
      Controller->HandleReplicatedTurnOwnership();
      Controller->HandleReplicatedPhaseChange(CurrentPhase);
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

ASkaldPlayerController *ATurnManager::FindControllerByPlayerId(int32 PlayerId) const {
  if (PlayerId == INDEX_NONE) {
    return nullptr;
  }

  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr : Controllers) {
    if (!ControllerPtr.IsValid()) {
      continue;
    }

    ASkaldPlayerController *Controller = ControllerPtr.Get();
    if (!Controller) {
      continue;
    }

    if (ASkaldPlayerState *PlayerState =
            Controller->GetPlayerState<ASkaldPlayerState>()) {
      if (PlayerState->GetPlayerId() == PlayerId) {
        return Controller;
      }
    }
  }

  return nullptr;
}

void ATurnManager::ClearActiveRetreatContext() { ActiveRetreatContext.Reset(); }

int32 ATurnManager::DistributeArmyPlacementUnits(ASkaldPlayerState *PlayerState) {
  if (!PlayerState) {
    return 0;
  }

  if (AWorldMap *WorldMap = ResolveWorldMap()) {
    return WorldMap->DistributeUnplacedArmyPlacementUnits(PlayerState);
  }

  return 0;
}
