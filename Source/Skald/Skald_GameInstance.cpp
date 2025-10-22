#include "Skald_GameInstance.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/CoreDelegates.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "UObject/CoreUObjectDelegates.h"
#else
#include "UObject/UObjectGlobals.h"
#endif
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameViewportClient.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "Skald_BattleGameMode.h"
#include "Skald_BattleLevelManager.h"
#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_PropertyAccess.h"
#include "Skald_TurnManager.h"
#include "TimerManager.h"
#include "Styling/CoreStyle.h"
#include "UI/SkaldUIHelpers.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Territory.h"
#include "WorldMap.h"

using Skald::PropertyAccess::ReadBoolProperty;
using Skald::PropertyAccess::ReadIntProperty;
using Skald::PropertyAccess::WriteBoolProperty;
using Skald::PropertyAccess::WriteIntProperty;

namespace {
constexpr float SnapshotRetryDelaySeconds = 0.01f;
}

void USkaldGameInstance::Init() {
  Super::Init();
  SeedCombatRandomStream(FMath::Rand());
  TakenFactions.Empty();
  if (Faction != ESkaldFaction::None) {
    TakenFactions.Add(Faction);
  }

  if (!PostWorldBeginPlayHandle.IsValid()) {
#if UE_VERSION_OLDER_THAN(5, 5, 0)
    PostWorldBeginPlayHandle =
        FWorldDelegates::OnWorldBeginPlay.AddUObject(
            this, &USkaldGameInstance::HandleWorldBeginPlay);
#else
    PostWorldBeginPlayHandle =
        FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
            this, &USkaldGameInstance::HandleWorldBeginPlay);
#endif
  }

  PendingBattle = FS_BattlePayload();
  PendingBattleResolution = FGridBattleResolution();
  bPendingBattleResolution = false;
  SetBattleMapActive(false);

  if (!BattleLevelStreamingManager) {
    BattleLevelStreamingManager = NewObject<USkaldBattleLevelManager>(this);
    BattleLevelStreamingManager->Initialise(this);
  }

  if (GEngine) {
    GEngine->OnNetworkFailure().AddUObject(
        this, &USkaldGameInstance::HandleNetworkFailure);
  }
}

void USkaldGameInstance::Shutdown() {
  if (PostWorldBeginPlayHandle.IsValid()) {
#if UE_VERSION_OLDER_THAN(5, 5, 0)
    FWorldDelegates::OnWorldBeginPlay.Remove(PostWorldBeginPlayHandle);
#else
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(
        PostWorldBeginPlayHandle);
#endif
    PostWorldBeginPlayHandle.Reset();
  }

  Super::Shutdown();
}

void USkaldGameInstance::SetTravelState(const FSkaldTravelState &InState) {
  TravelState = InState;
  TravelState.bValid = true;
  if (TravelState.CachedTerritories.Num() > 0) {
    CachedWorldMapTerritories = TravelState.CachedTerritories;
    PendingTravelTerritories = TravelState.CachedTerritories;
  }
  UE_LOG(LogSkald, Log,
         TEXT("GameInstance travel state set: Expected=%d Attacker=%d Defender=%d HumanTerritories=%d CachedTerritories=%d"),
         TravelState.ExpectedControllers, TravelState.AttackerTerritory,
         TravelState.DefenderTerritory, TravelState.HumanOwnedTerritories.Num(),
         TravelState.CachedTerritories.Num());

  if (TravelState.CachedTerritories.Num() == 0) {
    UE_LOG(LogSkald, Warning,
           TEXT("GameInstance travel state missing cached territories snapshot"));
  }
}

void USkaldGameInstance::SetPendingTravelSnapshot(
    const TArray<FS_Territory> &Snapshot) {
  PendingTravelTerritories = Snapshot;

  if (PendingTravelTerritories.Num() > 0 && CachedWorldMapTerritories.Num() == 0) {
    CachedWorldMapTerritories = PendingTravelTerritories;
  }

  UE_LOG(LogSkald, Verbose,
         TEXT("GameInstance pending travel snapshot set (%d territories)"),
         PendingTravelTerritories.Num());
}

void USkaldGameInstance::ClearPendingTravelSnapshot() {
  if (PendingTravelTerritories.Num() > 0) {
    PendingTravelTerritories.Reset();
    UE_LOG(LogSkald, Verbose,
           TEXT("GameInstance pending travel snapshot cleared"));
  }
}

void USkaldGameInstance::SetPendingReturnMap(const FString &InReturnMap) {
  if (PendingReturnMap.Equals(InReturnMap, ESearchCase::CaseSensitive)) {
    return;
  }

  PendingReturnMap = InReturnMap;

  const TCHAR *LoggedValue = PendingReturnMap.IsEmpty()
                                 ? TEXT("<Empty>")
                                 : *PendingReturnMap;
  UE_LOG(LogSkald, Log, TEXT("GameInstance pending return map set to %s"),
         LoggedValue);
}

void USkaldGameInstance::ClearPendingReturnMap() {
  if (PendingReturnMap.IsEmpty()) {
    return;
  }

  UE_LOG(LogSkald, Verbose,
         TEXT("GameInstance pending return map cleared (was '%s')"),
         *PendingReturnMap);
  PendingReturnMap.Reset();
}

void USkaldGameInstance::SetTravelPending(bool bInPending) {
  if (bTravelPending == bInPending) {
    return;
  }

  bTravelPending = bInPending;
  UE_LOG(LogSkald, Log, TEXT("GameInstance travel pending set: %s"),
         bTravelPending ? TEXT("true") : TEXT("false"));

  UGameViewportClient *Viewport = GetGameViewportClient();
  if (!Viewport) {
    if (!bTravelPending) {
      TravelLoadingOverlay.Reset();
    }
    return;
  }

  if (bTravelPending) {
    if (!TravelLoadingOverlay.IsValid()) {
      TSharedRef<SOverlay> Overlay = SNew(SOverlay)
          + SOverlay::Slot()
                .HAlign(HAlign_Fill)
                .VAlign(VAlign_Fill)[SNew(SImage).ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.7f))]
          + SOverlay::Slot()
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)[SNew(SBorder)
                                           .Padding(FMargin(40.f))
                                           .BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.85f))
                                           .HAlign(HAlign_Center)
                                           .VAlign(VAlign_Center)[SNew(STextBlock)
                                                                     .Justification(ETextJustify::Center)
                                                                     .Text(NSLOCTEXT("Skald", "TravelLoadingText", "Loading overworld..."))
                                                                     .Font(FCoreStyle::GetDefaultFontStyle("Bold", 32))
                                                                     .ColorAndOpacity(FLinearColor::White)]];

      TravelLoadingOverlay = Overlay;
      Viewport->AddViewportWidgetContent(Overlay, 100);
    }
  } else {
    if (TravelLoadingOverlay.IsValid()) {
      Viewport->RemoveViewportWidgetContent(TravelLoadingOverlay.ToSharedRef());
      TravelLoadingOverlay.Reset();
    }
  }
}

bool USkaldGameInstance::CacheWorldMapSnapshot(UWorld *InWorldContext) {
  UWorld *World = InWorldContext ? InWorldContext : GetWorld();
  if (!World || World->GetNetMode() == NM_Client) {
    return false;
  }

  if (bIsInBattleMap) {
    UE_LOG(LogSkald, Verbose,
           TEXT("GameInstance CacheWorldMapSnapshot skipped: currently on battle map"));
    World->GetTimerManager().ClearTimer(TerritorySnapshotRetryHandle);
    return false;
  }

  ASkaldGameMode *GameMode = World->GetAuthGameMode<ASkaldGameMode>();
  ATurnManager *TurnManager = nullptr;
  if (GameMode) {
    if (GameMode->TurnManager && !IsValid(GameMode->TurnManager)) {
      GameMode->TurnManager = nullptr;
    }
    TurnManager = GameMode->TurnManager;
  }

  if (!TurnManager) {
    if (AActor *Actor =
            UGameplayStatics::GetActorOfClass(World, ATurnManager::StaticClass())) {
      TurnManager = Cast<ATurnManager>(Actor);
    }
  }

  AWorldMap *WorldMap = nullptr;
  if (GameMode) {
    if (GameMode->WorldMap && !IsValid(GameMode->WorldMap)) {
      GameMode->WorldMap = nullptr;
    }
    WorldMap = GameMode->WorldMap;
  }

  if (!WorldMap) {
    if (AWorldMap *FoundMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
            World, AWorldMap::StaticClass()))) {
      if (IsValid(FoundMap)) {
        WorldMap = FoundMap;
      }
    }

    if (!WorldMap && TurnManager) {
      if (AWorldMap *CachedMap = TurnManager->GetCachedWorldMapActor()) {
        if (IsValid(CachedMap)) {
          WorldMap = CachedMap;
        }
      }
    }

    if (GameMode) {
      GameMode->WorldMap = WorldMap;
    }
  }

  if (WorldMap && !IsValid(WorldMap)) {
    WorldMap = nullptr;
    if (GameMode) {
      GameMode->WorldMap = nullptr;
    }
  }

  const int32 PreviousSnapshotCount = CachedWorldMapTerritories.Num();

  if (!WorldMap) {
    UE_LOG(LogSkald, Warning,
           TEXT("GameInstance CacheWorldMapSnapshot failed: WorldMap not found (keeping previous %d territories)"),
           PreviousSnapshotCount);
    ScheduleSnapshotRetry(World);
    return false;
  }

  TArray<FS_Territory> TerritorySnapshots;
  TerritorySnapshots.Reserve(WorldMap->Territories.Num());

  for (ATerritory *Territory : WorldMap->Territories) {
    if (!Territory) {
      continue;
    }

    FS_Territory TerrData;
    TerrData.TerritoryID = Territory->TerritoryID;
    TerrData.TerritoryName = Territory->TerritoryName;
    TerrData.OwnerPlayerID =
        Territory->OwningPlayer ? Territory->OwningPlayer->GetPlayerId() : 0;
    TerrData.IsCapital = Territory->bIsCapital;
    TerrData.CapitalOwner = TerrData.OwnerPlayerID;
    TerrData.ArmyUnits = Territory->ArmyUnits;
    TerrData.ContinentID = Territory->ContinentID;
    TerrData.AdjacentIDs.Reset();
    for (ATerritory *Adj : Territory->AdjacentTerritories) {
      if (Adj) {
        TerrData.AdjacentIDs.Add(Adj->TerritoryID);
      }
    }
    TerrData.Location = Territory->GetActorLocation();
    TerrData.HasTreasure = Territory->bHasTreasure;
    TerrData.TreasureAttachedUnitID =
        ReadIntProperty(Territory, TEXT("TreasureAttachedUnitID"));
    TerrData.FortificationLevel =
        ReadIntProperty(Territory, TEXT("FortificationLevel"));
    TerrData.Moat = ReadBoolProperty(Territory, TEXT("Moat"));
    TerrData.WallHealth =
        ReadIntProperty(Territory, TEXT("WallHealth"));
    TerrData.BuiltSiegeID = Territory->BuiltSiegeID;
    TerrData.ConqueredTurn =
        ReadIntProperty(Territory, TEXT("ConqueredTurn"));
    TerrData.IsNeutralSpawn =
        ReadBoolProperty(Territory, TEXT("IsNeutralSpawn"));

    TerritorySnapshots.Add(MoveTemp(TerrData));
  }

  bool bUsedTurnManagerFallback = false;
  if (TerritorySnapshots.Num() == 0 && TurnManager) {
    bUsedTurnManagerFallback =
        TurnManager->CaptureWorldSnapshot(TerritorySnapshots);
  }

  if (TerritorySnapshots.Num() == 0) {
    UE_LOG(LogSkald, Warning,
           TEXT("GameInstance CacheWorldMapSnapshot produced an empty snapshot; keeping previous %d territories"),
           PreviousSnapshotCount);
    ScheduleSnapshotRetry(World);
    return false;
  }

  World->GetTimerManager().ClearTimer(TerritorySnapshotRetryHandle);
  CachedWorldMapTerritories = MoveTemp(TerritorySnapshots);

  UE_LOG(LogSkald, Verbose,
         TEXT("GameInstance CacheWorldMapSnapshot captured %d territories (previously %d%s)"),
         CachedWorldMapTerritories.Num(), PreviousSnapshotCount,
         bUsedTurnManagerFallback ? TEXT(", via turn manager fallback")
                                  : TEXT(""));
  return true;
}

bool USkaldGameInstance::RestoreWorldFromSnapshot(UWorld *InWorldContext) {
  UWorld *World = InWorldContext ? InWorldContext : GetWorld();
  if (!World || World->GetNetMode() == NM_Client) {
    return false;
  }

  const TArray<FS_Territory> *SnapshotSource = nullptr;
  const TArray<FS_Territory> &PendingSnapshot = GetPendingTravelSnapshot();
  if (PendingSnapshot.Num() > 0) {
    SnapshotSource = &PendingSnapshot;
  } else if (CachedWorldMapTerritories.Num() > 0) {
    SnapshotSource = &CachedWorldMapTerritories;
  }

  if (!SnapshotSource) {
    UE_LOG(LogSkald, Warning,
           TEXT("GameInstance RestoreWorldFromSnapshot: No cached territory snapshot available."));
    bResumeTurns = false;
    return false;
  }

  ASkaldGameMode *GameMode = World->GetAuthGameMode<ASkaldGameMode>();
  if (!GameMode) {
    UE_LOG(LogSkald, Verbose,
           TEXT("GameInstance RestoreWorldFromSnapshot: GameMode not yet available."));
    return false;
  }

  if (GameMode->WorldMap && !IsValid(GameMode->WorldMap)) {
    GameMode->WorldMap = nullptr;
  }

  AWorldMap *WorldMap = GameMode->WorldMap;
  if (!WorldMap) {
    if (AWorldMap *FoundMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
            World, AWorldMap::StaticClass()))) {
      if (IsValid(FoundMap)) {
        WorldMap = FoundMap;
      }
    }
    if (WorldMap) {
      GameMode->WorldMap = WorldMap;
    }
  }

  if (WorldMap && !IsValid(WorldMap)) {
    GameMode->WorldMap = nullptr;
    WorldMap = nullptr;
  }

  if (!WorldMap) {
    UE_LOG(LogSkald, Verbose,
           TEXT("GameInstance RestoreWorldFromSnapshot: World map not yet available."));
    return false;
  }

  ASkaldGameState *GameState = World->GetGameState<ASkaldGameState>();
  if (!GameState) {
    UE_LOG(LogSkald, Warning,
           TEXT("GameInstance RestoreWorldFromSnapshot: GameState missing; retrying later."));
    return false;
  }

  TArray<int32> MissingPlayerIds;
  if (!ValidateSnapshotPlayers(*SnapshotSource, GameState, MissingPlayerIds)) {
    FString MissingList;
    for (int32 Index = 0; Index < MissingPlayerIds.Num(); ++Index) {
      if (Index > 0) {
        MissingList += TEXT(", ");
      }
      MissingList += FString::FromInt(MissingPlayerIds[Index]);
    }

    UE_LOG(LogSkald, Verbose,
           TEXT("GameInstance RestoreWorldFromSnapshot: Deferring until PlayerStates registered for IDs [%s]"),
           *MissingList);
    return false;
  }

  if (WorldMap->Territories.Num() == 0) {
    if (!WorldMap->GenerateTerritoriesFromTable()) {
      UE_LOG(LogSkald, Error,
             TEXT("GameInstance RestoreWorldFromSnapshot failed: Unable to generate territories."));
      return false;
    }
  }

  TMap<int32, ATerritory *> TerritoryById;
  TerritoryById.Reserve(WorldMap->Territories.Num());
  for (ATerritory *Territory : WorldMap->Territories) {
    if (!Territory) {
      continue;
    }
    TerritoryById.Add(Territory->TerritoryID, Territory);
    Territory->AdjacentTerritories.Reset();
  }

  TMap<int32, FS_PlayerData *> PlayerDataById;
  TMap<int32, ASkaldPlayerState *> PlayerStateById;

  for (APlayerState *PSBase : GameState->PlayerArray) {
    if (ASkaldPlayerState *SkaldPS = Cast<ASkaldPlayerState>(PSBase)) {
      PlayerStateById.Add(SkaldPS->GetPlayerId(), SkaldPS);
    }
  }

  for (FS_PlayerData &PlayerData : GameMode->PlayerDataArray) {
    PlayerDataById.Add(PlayerData.PlayerID, &PlayerData);
    PlayerData.IsEliminated = true;
    PlayerData.IsAlive = false;
    PlayerData.CapitalsOwned = 0;
    PlayerData.TroopsCount = 0;
    PlayerData.CapitalTerritoryIDs.Reset();
  }

  int32 RestoredCount = 0;
  FS_Territory SampleSnapshot;
  ATerritory *SampleActor = nullptr;
  bool bRecordedSample = false;
  for (const FS_Territory &Snapshot : *SnapshotSource) {
    ATerritory *const *FoundTerritory = TerritoryById.Find(Snapshot.TerritoryID);
    if (!FoundTerritory || !*FoundTerritory) {
      continue;
    }

    ATerritory *Territory = *FoundTerritory;
    Territory->TerritoryName = Snapshot.TerritoryName;
    Territory->ArmyUnits = Snapshot.ArmyUnits;
    Territory->bIsCapital = Snapshot.IsCapital;
    Territory->bHasTreasure = Snapshot.HasTreasure;
    WriteIntProperty(Territory, TEXT("TreasureAttachedUnitID"),
                     Snapshot.TreasureAttachedUnitID);
    WriteIntProperty(Territory, TEXT("FortificationLevel"),
                     Snapshot.FortificationLevel);
    WriteBoolProperty(Territory, TEXT("Moat"), Snapshot.Moat);
    WriteIntProperty(Territory, TEXT("WallHealth"), Snapshot.WallHealth);
    Territory->BuiltSiegeID = Snapshot.BuiltSiegeID;
    WriteIntProperty(Territory, TEXT("ConqueredTurn"),
                     Snapshot.ConqueredTurn);
    WriteBoolProperty(Territory, TEXT("IsNeutralSpawn"),
                      Snapshot.IsNeutralSpawn);
    Territory->SetActorLocation(Snapshot.Location);
    Territory->OwningPlayer =
        (Snapshot.OwnerPlayerID > 0)
            ? GameState->GetPlayerById(Snapshot.OwnerPlayerID)
            : nullptr;

    Territory->AdjacentTerritories.Reset();
    for (int32 NeighborId : Snapshot.AdjacentIDs) {
      if (ATerritory *Neighbor = TerritoryById.FindRef(NeighborId)) {
        Territory->AdjacentTerritories.AddUnique(Neighbor);
      }
    }

    Territory->RefreshAppearance();

    if (FS_PlayerData **PlayerDataPtr =
            PlayerDataById.Find(Snapshot.OwnerPlayerID)) {
      FS_PlayerData *PlayerData = *PlayerDataPtr;
      PlayerData->IsEliminated = false;
      PlayerData->IsAlive = true;
      PlayerData->TroopsCount += Snapshot.ArmyUnits;
      if (Snapshot.IsCapital) {
        PlayerData->CapitalsOwned += 1;
        PlayerData->CapitalTerritoryIDs.AddUnique(Snapshot.TerritoryID);
      }
    }

    ++RestoredCount;

    if (!bRecordedSample) {
      SampleSnapshot = Snapshot;
      SampleActor = Territory;
      bRecordedSample = true;
    }
  }

  if (RestoredCount == 0) {
    UE_LOG(LogSkald, Warning,
           TEXT("GameInstance RestoreWorldFromSnapshot: Cached snapshot contained no valid territories."));
    return false;
  }

  for (TPair<int32, ASkaldPlayerState *> &Entry : PlayerStateById) {
    ASkaldPlayerState *PlayerState = Entry.Value;
    if (!PlayerState) {
      continue;
    }

    const FS_PlayerData *const *PlayerDataPtr = PlayerDataById.Find(Entry.Key);
    const bool bShouldBeEliminated =
        !(PlayerDataPtr && *PlayerDataPtr) || (*PlayerDataPtr)->IsEliminated;

    bool bStateChanged = false;
    if (PlayerState->IsEliminated != bShouldBeEliminated) {
      PlayerState->IsEliminated = bShouldBeEliminated;
      bStateChanged = true;
    }

    if (bShouldBeEliminated && PlayerState->bHasLockedIn) {
      PlayerState->bHasLockedIn = false;
      bStateChanged = true;
    }

    if (bStateChanged) {
      PlayerState->ForceNetUpdate();
    }
  }

  WorldMap->SpawnedLocations.Reset();
  for (const FS_Territory &Snapshot : *SnapshotSource) {
    WorldMap->SpawnedLocations.Add(Snapshot.TerritoryID, Snapshot.Location);
  }

  if (SnapshotSource == &PendingSnapshot &&
      CachedWorldMapTerritories.Num() == 0) {
    CachedWorldMapTerritories = PendingSnapshot;
  }

  if (WorldMap->SelectedTerritory &&
      !TerritoryById.Contains(WorldMap->SelectedTerritory->TerritoryID)) {
    WorldMap->SelectedTerritory = nullptr;
  }

  GameState->OnPlayersUpdated.Broadcast();
  GameMode->RefreshHUDs();

  if (bRecordedSample && SampleActor) {
    UE_LOG(LogSkald, Verbose,
           TEXT("GameInstance RestoreWorldFromSnapshot sample (before): Id=%d Owner=%d Army=%d "
                "TreasureCarrier=%d Fort=%d Moat=%d Wall=%d Conquered=%d Neutral=%d"),
           SampleSnapshot.TerritoryID, SampleSnapshot.OwnerPlayerID,
           SampleSnapshot.ArmyUnits, SampleSnapshot.TreasureAttachedUnitID,
           SampleSnapshot.FortificationLevel, SampleSnapshot.Moat ? 1 : 0,
           SampleSnapshot.WallHealth, SampleSnapshot.ConqueredTurn,
           SampleSnapshot.IsNeutralSpawn ? 1 : 0);

    const int32 PostOwnerId =
        SampleActor->OwningPlayer ? SampleActor->OwningPlayer->GetPlayerId() : 0;
    UE_LOG(LogSkald, Verbose,
           TEXT("GameInstance RestoreWorldFromSnapshot sample (after): Id=%d Owner=%d Army=%d "
                "TreasureCarrier=%d Fort=%d Moat=%d Wall=%d Conquered=%d Neutral=%d"),
           SampleActor->TerritoryID, PostOwnerId, SampleActor->ArmyUnits,
           ReadIntProperty(SampleActor, TEXT("TreasureAttachedUnitID")),
           ReadIntProperty(SampleActor, TEXT("FortificationLevel")),
           ReadBoolProperty(SampleActor, TEXT("Moat")) ? 1 : 0,
           ReadIntProperty(SampleActor, TEXT("WallHealth")),
           ReadIntProperty(SampleActor, TEXT("ConqueredTurn")),
           ReadBoolProperty(SampleActor, TEXT("IsNeutralSpawn")) ? 1 : 0);
  }

  UE_LOG(LogSkald, Log,
         TEXT("GameInstance RestoreWorldFromSnapshot: Restored %d territories from cached data."),
         RestoredCount);
  return true;
}

bool USkaldGameInstance::ValidateSnapshotPlayers(
    const TArray<FS_Territory> &Snapshot, ASkaldGameState *GameState,
    TArray<int32> &OutMissingPlayerIds) const {
  OutMissingPlayerIds.Reset();
  if (!GameState) {
    return false;
  }

  TSet<int32> RequiredPlayerIds;
  for (const FS_Territory &Entry : Snapshot) {
    if (Entry.OwnerPlayerID > 0) {
      RequiredPlayerIds.Add(Entry.OwnerPlayerID);
    }
  }

  for (int32 PlayerId : RequiredPlayerIds) {
    if (!GameState->GetPlayerById(PlayerId)) {
      OutMissingPlayerIds.Add(PlayerId);
    }
  }

  return OutMissingPlayerIds.Num() == 0;
}

void USkaldGameInstance::ScheduleSnapshotRetry(UWorld *World) {
  if (!World) {
    TerritorySnapshotRetryHandle.Invalidate();
    return;
  }

  FTimerManager &WorldTimerManager = World->GetTimerManager();
  if (WorldTimerManager.IsTimerActive(TerritorySnapshotRetryHandle)) {
    return;
  }

  FTimerDelegate RetryDelegate = FTimerDelegate::CreateUObject(
      this, &USkaldGameInstance::HandleCacheWorldMapSnapshotRetry);
  WorldTimerManager.SetTimer(TerritorySnapshotRetryHandle, RetryDelegate,
                             SnapshotRetryDelaySeconds, false);
}

void USkaldGameInstance::HandleCacheWorldMapSnapshotRetry() {
  CacheWorldMapSnapshot();
}

void USkaldGameInstance::HandleWorldBeginPlay(UWorld *LoadedWorld) {
  if (!LoadedWorld || LoadedWorld->GetGameInstance() != this) {
    return;
  }

  UE_LOG(LogSkald, Log,
         TEXT("HandleWorldBeginPlay: World=%s NetMode=%d"),
         *GetNameSafe(LoadedWorld),
         static_cast<int32>(LoadedWorld->GetNetMode()));

  SetTravelPending(false);

  if (LoadedWorld->GetNetMode() == NM_Client) {
    return;
  }

  LoadedWorld->GetTimerManager().ClearTimer(PendingResumeDelayHandle);

  const bool bShouldResume = ShouldAttemptTravelResume();
  UE_LOG(LogSkald, Log,
         TEXT("HandleWorldBeginPlay: ShouldResume=%d PendingSnapshot=%d CachedTravel=%d CachedWorldMap=%d"),
         bShouldResume ? 1 : 0, PendingTravelTerritories.Num(),
         TravelState.CachedTerritories.Num(), CachedWorldMapTerritories.Num());
  if (bShouldResume) {
    constexpr float TravelResumeDelaySeconds = 2.0f;
    FTimerDelegate DeferredResume = FTimerDelegate::CreateUObject(
        this, &USkaldGameInstance::HandleDeferredTravelResume, LoadedWorld);
    LoadedWorld->GetTimerManager().SetTimer(
        PendingResumeDelayHandle, DeferredResume, TravelResumeDelaySeconds,
        false);
  } else {
    if (PendingResumeWorld.IsValid()) {
      if (UWorld *World = PendingResumeWorld.Get()) {
        World->GetTimerManager().ClearTimer(PendingResumeRetryHandle);
      }
    }
    PendingResumeWorld.Reset();
    LoadedWorld->GetTimerManager().ClearTimer(PendingResumeRetryHandle);
    LoadedWorld->GetTimerManager().ClearTimer(PendingResumeDelayHandle);
  }

  if (bPendingBattleResolution && PendingBattleResolution.bValid) {
    RequestPendingBattleResolution(LoadedWorld);
  }

  if (LoadedWorld->GetNetMode() == NM_Client) {
    return;
  }

  ASkaldGameMode *GameMode = LoadedWorld->GetAuthGameMode<ASkaldGameMode>();
  if (!GameMode) {
    return;
  }

  if (!GameMode->IsWorldInitialized() && bShouldResume) {
    FTimerDelegate InitDelegate = FTimerDelegate::CreateUObject(
        GameMode, &ASkaldGameMode::TryInitializeWorldAndStart);
    LoadedWorld->GetTimerManager().SetTimerForNextTick(InitDelegate);
  }
}

void USkaldGameInstance::HandleDeferredTravelResume(UWorld *LoadedWorld) {
  if (!LoadedWorld || LoadedWorld->GetGameInstance() != this) {
    return;
  }

  UE_LOG(LogSkald, Log,
         TEXT("HandleDeferredTravelResume: World=%s NetMode=%d"),
         *GetNameSafe(LoadedWorld),
         static_cast<int32>(LoadedWorld->GetNetMode()));

  LoadedWorld->GetTimerManager().ClearTimer(PendingResumeDelayHandle);

  if (!ShouldAttemptTravelResume()) {
    UE_LOG(LogSkald, Verbose,
           TEXT("HandleDeferredTravelResume: resume no longer required"));
    return;
  }

  UE_LOG(LogSkald, Verbose,
         TEXT("HandleDeferredTravelResume: scheduling resume for %s"),
         *GetNameSafe(LoadedWorld));
  ScheduleTravelResume(LoadedWorld);
}

bool USkaldGameInstance::ShouldAttemptTravelResume() const {
  const bool bHasPendingSnapshot = PendingTravelTerritories.Num() > 0;
  const bool bHasTravelCache =
      TravelState.bValid && TravelState.CachedTerritories.Num() > 0;
  const bool bHasCachedWorldMap = CachedWorldMapTerritories.Num() > 0;
  const bool bHasPendingResolution =
      bPendingBattleResolution || PendingBattleResolution.bValid;
  const bool bShouldResume = bHasPendingSnapshot || bHasTravelCache ||
                             bHasCachedWorldMap || bResumeTurns ||
                             bHasPendingResolution;

  UE_LOG(LogSkald, Verbose,
         TEXT(
             "ShouldAttemptTravelResume: PendingSnapshot=%d TravelCacheValid=%d CachedWorldMap=%d ResumeTurns=%d PendingResolution=%d -> %d"),
         bHasPendingSnapshot ? 1 : 0, bHasTravelCache ? 1 : 0,
         bHasCachedWorldMap ? 1 : 0, bResumeTurns ? 1 : 0,
         bHasPendingResolution ? 1 : 0, bShouldResume ? 1 : 0);

  return bShouldResume;
}

void USkaldGameInstance::ScheduleTravelResume(UWorld *World) {
  if (!World || World->GetNetMode() == NM_Client) {
    UE_LOG(LogSkald, Warning,
           TEXT(
               "ScheduleTravelResume aborted: World=%s NetMode=%d (expected server)"),
           *GetNameSafe(World), World ? static_cast<int32>(World->GetNetMode()) : -1);
    return;
  }

  PendingResumeWorld = World;
  World->GetTimerManager().ClearTimer(PendingResumeRetryHandle);

  FTimerDelegate ResumeDelegate = FTimerDelegate::CreateUObject(
      this, &USkaldGameInstance::AttemptResumeAfterTravel);
  World->GetTimerManager().SetTimerForNextTick(ResumeDelegate);

  UE_LOG(LogSkald, Log,
         TEXT("ScheduleTravelResume: World=%s PendingSnapshot=%d CachedTravel=%d CachedWorldMap=%d"),
         *GetNameSafe(World), PendingTravelTerritories.Num(),
         TravelState.CachedTerritories.Num(), CachedWorldMapTerritories.Num());
}

void USkaldGameInstance::AttemptResumeAfterTravel() {
  UWorld *World = PendingResumeWorld.Get();
  if (!World || World->GetNetMode() == NM_Client) {
    UE_LOG(LogSkald, Warning,
           TEXT("AttemptResumeAfterTravel aborted: World=%s NetMode=%d"),
           *GetNameSafe(World),
           World ? static_cast<int32>(World->GetNetMode()) : -1);
    PendingResumeWorld.Reset();
    return;
  }

  UE_LOG(LogSkald, Log,
         TEXT(
             "AttemptResumeAfterTravel: World=%s PendingSnapshot=%d TravelCache=%d CachedWorldMap=%d ResumeTurns=%d PendingResolution=%d"),
         *GetNameSafe(World), PendingTravelTerritories.Num(),
         TravelState.CachedTerritories.Num(), CachedWorldMapTerritories.Num(),
         bResumeTurns ? 1 : 0,
         (bPendingBattleResolution || PendingBattleResolution.bValid) ? 1 : 0);

  World->GetTimerManager().ClearTimer(PendingResumeRetryHandle);

  if (!ShouldAttemptTravelResume()) {
    UE_LOG(LogSkald, Verbose,
           TEXT("AttemptResumeAfterTravel: resume no longer required"));
    PendingResumeWorld.Reset();
    return;
  }

  ASkaldGameMode *GameMode = World->GetAuthGameMode<ASkaldGameMode>();
  if (!GameMode) {
    UE_LOG(LogSkald, Warning,
           TEXT(
               "AttemptResumeAfterTravel: GameMode missing in %s, retrying shortly"),
           *GetNameSafe(World));
    constexpr float RetryDelaySeconds = 0.05f;
    FTimerDelegate RetryDelegate = FTimerDelegate::CreateUObject(
        this, &USkaldGameInstance::AttemptResumeAfterTravel);
    World->GetTimerManager().SetTimer(PendingResumeRetryHandle, RetryDelegate,
                                      RetryDelaySeconds, false);
    return;
  }

  if (!GameMode->IsWorldInitialized()) {
    UE_LOG(LogSkald, Log,
           TEXT("AttemptResumeAfterTravel: initializing world via %s"),
           *GetNameSafe(GameMode));
    GameMode->TryInitializeWorldAndStart();
    if (!GameMode->IsWorldInitialized()) {
      UE_LOG(LogSkald, Verbose,
             TEXT("AttemptResumeAfterTravel: world still not initialized, scheduling retry"));
      constexpr float RetryDelaySeconds = 0.05f;
      FTimerDelegate RetryDelegate = FTimerDelegate::CreateUObject(
          this, &USkaldGameInstance::AttemptResumeAfterTravel);
      World->GetTimerManager().SetTimer(PendingResumeRetryHandle, RetryDelegate,
                                        RetryDelaySeconds, false);
      return;
    }
  }

  UE_LOG(LogSkald, Log,
         TEXT("AttemptResumeAfterTravel: resume complete for world %s"),
         *GetNameSafe(World));
  PendingResumeWorld.Reset();
}

void USkaldGameInstance::SetActiveBattleGameMode(
    ASkald_BattleGameMode *InGameMode) {
  ASkald_BattleGameMode *Previous = ActiveBattleGameMode.Get();
  if (Previous == InGameMode) {
    return;
  }

  if (!InGameMode) {
    if (Previous) {
      UE_LOG(LogSkald, Log,
             TEXT("GameInstance cleared active battle game mode %s"),
             *GetNameSafe(Previous));
    }
    ActiveBattleGameMode = nullptr;
    return;
  }

  ActiveBattleGameMode = InGameMode;
  UE_LOG(LogSkald, Log,
         TEXT("GameInstance set active battle game mode: %s"),
         *GetNameSafe(InGameMode));
}

void USkaldGameInstance::SetBattleMapActive(bool bInBattleMap) {
  if (bIsInBattleMap == bInBattleMap) {
    return;
  }

  bIsInBattleMap = bInBattleMap;
  OnBattleMapStateChanged.Broadcast(bIsInBattleMap);

  if (bIsInBattleMap) {
    if (UWorld *World = GetWorld()) {
      World->GetTimerManager().ClearTimer(TerritorySnapshotRetryHandle);
    } else {
      TerritorySnapshotRetryHandle.Invalidate();
    }
  }
}

void USkaldGameInstance::SeedCombatRandomStream(int32 Seed) {
  CombatRandomStream.Initialize(Seed);
}

void USkaldGameInstance::ShowDeployWidget() {
  if (!DeployWidget) {
    TSubclassOf<UUserWidget> WidgetClass = DeployWidgetClass;
    if (!WidgetClass) {
      UE_LOG(LogSkald, Warning,
             TEXT("ShowDeployWidget: DeployWidgetClass not set."));
      return;
    }

    DeployWidget = CreateWidget<UUserWidget>(this, WidgetClass);
  }

  if (!DeployWidget) {
    return;
  }

  if (!DeployWidget->IsInViewport()) {
    DeployWidget->AddToViewport();
    if (UWorld *World = GetWorld()) {
      if (APlayerController *PC = World->GetFirstPlayerController()) {
        FocusWidgetUIOnly(PC, DeployWidget);
      }
    }
  }
}

void USkaldGameInstance::HideDeployWidget() {
  if (!DeployWidget) {
    return;
  }

  if (DeployWidget->IsInViewport()) {
    DeployWidget->RemoveFromParent();
  }

  if (UWorld *World = GetWorld()) {
    if (ASkaldPlayerController *PC =
            Cast<ASkaldPlayerController>(World->GetFirstPlayerController())) {
      PC->ShowMainHUD();
    }
  }
}

void USkaldGameInstance::HandleNetworkFailure(
    UWorld *World, UNetDriver * /*Driver*/,
    ENetworkFailure::Type /*FailureType*/, const FString &ErrorString) {
  if (GEngine) {
    GEngine->AddOnScreenDebugMessage(
        -1, 5.f, FColor::Red,
        FString::Printf(TEXT("Network failure: %s"), *ErrorString));
  }

  bIsMultiplayer = false;
  bIsHost = false;

  ResetSessionState();

  if (World) {
    const FName LobbyMap(TEXT("/Game/Blueprints/Maps/Skald_Lobby"));
    UGameplayStatics::OpenLevel(World, LobbyMap);
  }
}

void USkaldGameInstance::ReturnToMainMenu() {
  ResetSessionState();

  if (UWorld *World = GetWorld()) {
    const FName LobbyMap(TEXT("/Game/Blueprints/Maps/Skald_Lobby"));
    UGameplayStatics::OpenLevel(World, LobbyMap);
  }
}

void USkaldGameInstance::RequestPendingBattleResolution(UWorld *LoadedWorld) {
  if (!bPendingBattleResolution || !PendingBattleResolution.bValid) {
    return;
  }

  UWorld *World = LoadedWorld ? LoadedWorld : GetWorld();
  if (!World) {
    return;
  }

  World->GetTimerManager().ClearTimer(PendingBattleResolutionKickoffHandle);

  FTimerDelegate ResolveDelegate = FTimerDelegate::CreateWeakLambda(
      this, [this]() { AttemptResolvePendingBattle(0); });
  World->GetTimerManager().SetTimerForNextTick(ResolveDelegate);
}

void USkaldGameInstance::AttemptResolvePendingBattle(int32 Attempt) {
  if (!bPendingBattleResolution || !PendingBattleResolution.bValid) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World || World->GetNetMode() == NM_Client) {
    return;
  }

  ATurnManager *TurnManager = nullptr;
  if (ASkaldGameMode *GameMode = World->GetAuthGameMode<ASkaldGameMode>()) {
    TurnManager = GameMode->GetTurnManager();
  }
  if (!TurnManager) {
    if (AActor *Actor =
            UGameplayStatics::GetActorOfClass(World, ATurnManager::StaticClass())) {
      TurnManager = Cast<ATurnManager>(Actor);
    }
  }

  if (TurnManager) {
    World->GetTimerManager().ClearTimer(PendingBattleResolutionKickoffHandle);
    TurnManager->ResolveGridBattleResult();
    return;
  }

  constexpr int32 MaxAttempts = 40;
  if (Attempt + 1 >= MaxAttempts) {
    UE_LOG(LogSkald, Warning,
           TEXT("GameInstance pending battle resolution could not locate a turn manager after %d attempts."),
           Attempt + 1);
    return;
  }

  constexpr float RetryDelaySeconds = 0.05f;
  FTimerDelegate RetryDelegate = FTimerDelegate::CreateWeakLambda(
      this, [this, Attempt]() { AttemptResolvePendingBattle(Attempt + 1); });
  World->GetTimerManager().SetTimer(PendingBattleResolutionKickoffHandle,
                                    RetryDelegate, RetryDelaySeconds, false);
}

void USkaldGameInstance::ResetSessionState() {
  // Clear any per-session state so a fresh lobby is created.
  JoinAddress.Empty();
  AIPlayersToSpawn = 1;
  TakenFactions.Empty();
  if (Faction != ESkaldFaction::None) {
    TakenFactions.Add(Faction);
  }
  OnFactionsUpdated.Broadcast();

  PendingBattle = FS_BattlePayload();
  PendingBattleResolution = FGridBattleResolution();
  bPendingBattleResolution = false;
  GridBattleManager = nullptr;
  if (BattleLevelStreamingManager) {
    BattleLevelStreamingManager->ReleaseBattleLevel();
  }
  if (ASkald_BattleGameMode *BattleGM = ActiveBattleGameMode.Get()) {
    if (!BattleGM->IsActorBeingDestroyed()) {
      BattleGM->Destroy();
    }
  }
  SetActiveBattleGameMode(nullptr);
  SetBattleMapActive(false);
  bTravelPending = false;
  PendingReturnMap.Reset();
  CachedWorldMapTerritories.Empty();
  PendingTravelTerritories.Empty();
  TravelState = FSkaldTravelState();

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(TerritorySnapshotRetryHandle);
  } else {
    TerritorySnapshotRetryHandle.Invalidate();
  }

  if (PendingResumeWorld.IsValid()) {
    if (UWorld *World = PendingResumeWorld.Get()) {
      World->GetTimerManager().ClearTimer(PendingResumeRetryHandle);
    }
    PendingResumeWorld.Reset();
  }
  PendingResumeRetryHandle.Invalidate();

  SeedCombatRandomStream(FMath::Rand());
  SavedTurnIndex = 0;
  SavedTurnPlayerId = 0;
  SavedTurnPhase = ETurnPhase::Reinforcement;
  bResumeTurns = false;
  LoadedSaveGame = nullptr;

  if (DeployWidget) {
    DeployWidget->RemoveFromParent();
    DeployWidget = nullptr;
  }
}
