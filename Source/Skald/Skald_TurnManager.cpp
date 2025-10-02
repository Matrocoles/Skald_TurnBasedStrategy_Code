#include "Skald_TurnManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GridBattleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "Skald_GameInstance.h"
#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_PropertyAccess.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Territory.h"
#include "UI/SkaldMainHUDWidget.h"
#include "UObject/UnrealType.h"
#include "WorldMap.h"

namespace {
FString GetResolvedPlayerName(const ASkaldPlayerState *PlayerState,
                              const TCHAR *Context) {
  if (!PlayerState) {
    return TEXT("Unknown");
  }

  return PlayerState->GetResolvedPlayerName(Context);
}

FString NormalizeMapName(UWorld *World, FString Candidate) {
  FString Result = MoveTemp(Candidate);

  if (Result.IsEmpty() && World) {
    Result = World->URL.Map;
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

    FString LongPackageName;
    if (FPackageName::IsShortPackageName(Result)) {
      if (FPackageName::SearchForPackageOnDisk(Result, &LongPackageName)) {
        Result = MoveTemp(LongPackageName);
      }
    } else if (!FPackageName::IsValidLongPackageName(Result)) {
      if (FPackageName::TryConvertFilenameToLongPackageName(Result,
                                                           LongPackageName)) {
        Result = MoveTemp(LongPackageName);
      }
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
}

void ATurnManager::BeginPlay() {
  Super::BeginPlay();

  CachedWorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));

  const bool bOnWorldMap = (CachedWorldMap != nullptr);

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    // Only resolve results when we are back on the world map
    if (bOnWorldMap && (GI->GridBattleManager || GI->bPendingBattleResolution)) {
      ResolveGridBattleResult();
    }

    // On the battle map, listen for battle end and travel back on event
    if (!bOnWorldMap && GI->GridBattleManager) {
      GI->GridBattleManager->OnBattleEnded.AddDynamic(
          this, &ATurnManager::HandleGridBattleEnded);
    }

    if (bOnWorldMap) {
      TryResumeSavedTurnState(GI);
    }
  }

  if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
    if (!GM->IsWorldInitialized()) {
      GM->TryInitializeWorldAndStart();
    }
  }
}

void ATurnManager::HandleGridBattleEnded(ESkaldFaction /*WinningFaction*/, int32 /*AttackerCasualties*/, int32 /*DefenderCasualties*/) {
  UWorld *World = GetWorld();

  FString ReturnMapName;
  if (!PendingBattle.ReturnMap.IsEmpty()) {
    ReturnMapName = PendingBattle.ReturnMap;
  } else if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    ReturnMapName = GI->PendingBattle.ReturnMap;
  }

  ReturnMapName = NormalizeMapName(World, MoveTemp(ReturnMapName));

  if (ReturnMapName.IsEmpty() && World) {
    ReturnMapName = NormalizeMapName(
        World, UGameplayStatics::GetCurrentLevelName(World, true));
  }

  if (ReturnMapName.IsEmpty()) {
    ReturnMapName = UGameplayStatics::GetCurrentLevelName(this, true);
  }

  ResolveGridBattleResult();

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->SetTravelPending(true);
  }

  if (!World) {
    World = GetWorld();
  }

  if (World) {
    const ENetMode NetMode = World->GetNetMode();

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
}

void ATurnManager::RegisterController(ASkaldPlayerController *Controller) {
  if (IsValid(Controller) && !Controllers.Contains(Controller)) {
    Controllers.Add(Controller);
    Controller->SetTurnManager(this);
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
  BroadcastCurrentPhase();
}

void ATurnManager::ApplyReinforcementsAndResources(ASkaldPlayerState *PS,
                                                   const TCHAR *Caller) {
  if (!PS) {
    return;
  }
  int32 Owned = 0;
  int32 ResourceGain = 0;
  if (CachedWorldMap) {
    if (CachedWorldMap->Territories.Num() == 0) {
      UE_LOG(LogSkald, Error, TEXT("%s: WorldMap %s has no territories"),
             Caller, *CachedWorldMap->GetName());
      if (GEngine) {
        GEngine->AddOnScreenDebugMessage(
            -1, 5.f, FColor::Red,
            FString::Printf(TEXT("%s: %s has no territories"), Caller,
                            *CachedWorldMap->GetName()));
      }
    } else {
      for (ATerritory *Terr : CachedWorldMap->Territories) {
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
  if (!Controllers.IsValidIndex(SavedIndex) || !Controllers[SavedIndex].IsValid()) {
    return false;
  }

  ASkaldPlayerController *Controller = Controllers[SavedIndex].Get();
  if (!Controller) {
    return false;
  }

  CurrentIndex = SavedIndex;
  CurrentPhase = GI->SavedTurnPhase;
  GI->bResumeTurns = false;

  SyncGameStateTurnIndex();
  Controller->StartTurn();
  BroadcastCurrentPhase();

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

  if (!CachedWorldMap || CachedWorldMap->Territories.Num() == 0) {
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

void ATurnManager::TriggerGridBattle(const FS_BattlePayload &Battle) {
  FS_BattlePayload SeededBattle = Battle;
  SeededBattle.RandomSeed = FMath::Rand();
  if (UWorld *World = GetWorld()) {
    FString ReturnMap = NormalizeMapName(World, FString());
    if (ReturnMap.IsEmpty()) {
      ReturnMap = NormalizeMapName(
          World, UGameplayStatics::GetCurrentLevelName(World, true));
    }
    SeededBattle.ReturnMap = MoveTemp(ReturnMap);
    if (ASkaldGameMode *GameMode = World->GetAuthGameMode<ASkaldGameMode>()) {
      GameMode->CacheWorldMapSnapshot();
    }
  }
  PendingBattle = SeededBattle;

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->SeedCombatRandomStream(SeededBattle.RandomSeed);
    GI->PendingBattleResolution = FGridBattleResolution();
    GI->bPendingBattleResolution = false;
    if (!GI->GridBattleManager) {
      GI->GridBattleManager = NewObject<UGridBattleManager>(GI);
    }
  }

  // Save the current turn state so it can be restored after travelling.
  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->SavedTurnIndex = CurrentIndex;
    GI->SavedTurnPhase = CurrentPhase;
    GI->bResumeTurns = true;
  }

  // Load a battle map where the grid based combat takes place.
  if (UWorld *World = GetWorld()) {
    FString MapToLoad = TEXT("BattleMap");
    if (BattleMaps.Num() > 0) {
      const int32 Index = FMath::RandRange(0, BattleMaps.Num() - 1);
      const FString Selected =
          BattleMaps[Index].ToSoftObjectPath().GetLongPackageName();
      if (!Selected.IsEmpty()) {
        MapToLoad = Selected;
      }
    }

    USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
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

    auto AppendHumanOwnership = [&](ASkaldPlayerState *OwnerPS, int32 TerritoryID) {
      if (OwnerPS && !OwnerPS->bIsAI && TerritoryID > 0) {
        TravelState.HumanOwnedTerritories.AddUnique(TerritoryID);
      }
    };

    TArray<FS_Territory> TerritorySnapshots;
    bool bUsedCachedFallback = false;
    if (CachedWorldMap) {
      TerritorySnapshots.Reserve(CachedWorldMap->Territories.Num());
      for (ATerritory *Territory : CachedWorldMap->Territories) {
        if (!Territory) {
          continue;
        }

        ASkaldPlayerState *OwnerPS = Territory->OwningPlayer;
        AppendHumanOwnership(OwnerPS, Territory->TerritoryID);

        FS_Territory Snapshot;
        Snapshot.TerritoryID = Territory->TerritoryID;
        Snapshot.TerritoryName = Territory->TerritoryName;
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
        Snapshot.WallHealth =
            ReadIntProperty(Territory, TEXT("WallHealth"));
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

        TerritorySnapshots.Add(MoveTemp(Snapshot));
      }
    } else if (GI && GI->CachedWorldMapTerritories.Num() > 0) {
      bUsedCachedFallback = true;
      TerritorySnapshots = GI->CachedWorldMapTerritories;
      for (const FS_Territory &Snapshot : TerritorySnapshots) {
        if (Snapshot.OwnerPlayerID > 0 && GS) {
          if (ASkaldPlayerState *OwnerPS = GS->GetPlayerById(Snapshot.OwnerPlayerID)) {
            AppendHumanOwnership(OwnerPS, Snapshot.TerritoryID);
          }
        }
      }
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

    TravelState.CachedTerritories = MoveTemp(TerritorySnapshots);

    FS_BattlePayload PendingPayload = SeededBattle;
    PendingPayload.FromTerritoryID = SeededBattle.FromTerritoryID;
    PendingPayload.TargetTerritoryID = SeededBattle.TargetTerritoryID;

    auto FindTerritory = [&](int32 TerritoryId) -> ATerritory * {
      if (!CachedWorldMap) {
        return nullptr;
      }
      for (ATerritory *Territory : CachedWorldMap->Territories) {
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

    if (PendingPayload.DefenderArmyCount <= 0) {
      if (ATerritory *DefTerritory = FindTerritory(PendingPayload.TargetTerritoryID)) {
        PendingPayload.DefenderArmyCount = DefTerritory->ArmyUnits;
      }
    }
    if (PendingPayload.DefenderArmyCount <= 0) {
      PendingPayload.DefenderArmyCount = PendingPayload.ArmyCountSent;
    }

    PendingBattle = PendingPayload;
    if (GI) {
      GI->SetTravelState(TravelState);
      GI->PendingBattle = PendingPayload;
      GI->SetTravelPending(true);
      GI->bIsInBattleMap = true;
    }

    UE_LOG(LogSkald, Log,
           TEXT("TriggerGridBattle: AttackerID=%d Name=%s Budget=%d DefenderID=%d Name=%s Budget=%d HumanOwned=%d CachedTerritories=%d"),
           PendingPayload.AttackerPlayerID, *PendingPayload.AttackerDisplayName,
           PendingPayload.ArmyCountSent, PendingPayload.DefenderPlayerID,
           *PendingPayload.DefenderDisplayName,
           PendingPayload.DefenderArmyCount,
           TravelState.HumanOwnedTerritories.Num(),
           TravelState.CachedTerritories.Num());

    if (IsRunningDedicatedServer() || World->GetNetMode() != NM_Standalone) {
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

void ATurnManager::ResolveGridBattleResult_Implementation() {
  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI) {
    return;
  }

  GI->bIsInBattleMap = false;

  // Always mirror the pending payload locally for reference.
  PendingBattle = GI->PendingBattle;

  // Capture the battle results from the active manager if available.
  if (GI->GridBattleManager) {
    FGridBattleResolution Resolution;
    Resolution.bValid = true;
    Resolution.AttackerSurvivorArmyCost =
        GI->GridBattleManager->GetAttackerSurvivorCost();
    Resolution.DefenderSurvivorArmyCost =
        GI->GridBattleManager->GetDefenderSurvivorCost();
    Resolution.AttackerCasualties = GI->GridBattleManager->GetAttackerInitialArmyCost() -
                                    Resolution.AttackerSurvivorArmyCost;
    Resolution.DefenderCasualties = GI->GridBattleManager->GetDefenderInitialArmyCost() -
                                    Resolution.DefenderSurvivorArmyCost;

    const FS_BattlePayload &Battle = GI->PendingBattle;
    ASkaldGameState *GS = GetWorld()->GetGameState<ASkaldGameState>();
    ESkaldFaction AttackerFaction = ESkaldFaction::None;
    ESkaldFaction DefenderFaction = ESkaldFaction::None;
    if (GS) {
      if (ASkaldPlayerState *AttackerPS = GS->GetPlayerById(Battle.AttackerPlayerID)) {
        AttackerFaction = AttackerPS->Faction;
      }
      if (ASkaldPlayerState *DefenderPS = GS->GetPlayerById(Battle.DefenderPlayerID)) {
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

    GI->PendingBattleResolution = Resolution;
    GI->bPendingBattleResolution = true;
    GI->GridBattleManager = nullptr;
  }

  if (!GI->bPendingBattleResolution || !GI->PendingBattleResolution.bValid) {
    return;
  }

  if (!CachedWorldMap) {
    return;
  }

  const FS_BattlePayload Battle = GI->PendingBattle;
  ATerritory *Source = CachedWorldMap->GetTerritoryById(Battle.FromTerritoryID);
  ATerritory *Target =
      CachedWorldMap->GetTerritoryById(Battle.TargetTerritoryID);
  if (!Source || !Target) {
    return;
  }

  FGridBattleResolution Resolution = GI->PendingBattleResolution;

  const int32 InitialSourceArmy = Source->ArmyUnits;
  const int32 InitialTargetArmy = Target->ArmyUnits;

  Source->ArmyUnits = FMath::Max(0, InitialSourceArmy - Battle.ArmyCountSent);

  if (Resolution.AttackerSurvivorArmyCost > 0 &&
      Resolution.DefenderSurvivorArmyCost <= 0) {
    Target->OwningPlayer = Source->OwningPlayer;
    Target->ArmyUnits = Resolution.AttackerSurvivorArmyCost;
  } else {
    Target->ArmyUnits = Resolution.DefenderSurvivorArmyCost;
  }

  Resolution.SourceArmyRemaining = Source->ArmyUnits;
  Resolution.TargetArmyRemaining = Target->ArmyUnits;

  Resolution.AttackerCasualties =
      InitialSourceArmy - (Source->ArmyUnits + Resolution.AttackerSurvivorArmyCost);
  Resolution.DefenderCasualties =
      InitialTargetArmy - Resolution.DefenderSurvivorArmyCost;

  Source->RefreshAppearance();
  Target->RefreshAppearance();

  GI->PendingBattle = FS_BattlePayload();
  PendingBattle = FS_BattlePayload();

  const int32 WinningPlayerID = Resolution.WinningPlayerID;
  const int32 NewOwnerPlayerID = Resolution.NewOwnerPlayerID;
  const int32 AttackerCasualties = Resolution.AttackerCasualties;
  const int32 DefenderCasualties = Resolution.DefenderCasualties;

  GI->bPendingBattleResolution = false;
  GI->PendingBattleResolution = FGridBattleResolution();

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
  if (CachedWorldMap) {
    ATerritory *Source = CachedWorldMap->GetTerritoryById(FromTerritoryID);
    ATerritory *Target = CachedWorldMap->GetTerritoryById(TargetTerritoryID);
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
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  if (CurrentPhase == ETurnPhase::ArmyPlacement) {
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

void ATurnManager::BroadcastCurrentPhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending || GI->bIsInBattleMap) {
        return;
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
}
