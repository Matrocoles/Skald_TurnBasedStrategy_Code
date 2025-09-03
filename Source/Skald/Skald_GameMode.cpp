#include "Skald_GameMode.h"
#include "Algo/RandomShuffle.h"
#include "Camera/CameraComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GridBattleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Skald.h"
#include "SkaldSaveGame.h"
#include "Skald_GameInstance.h"
#include "Skald_GameState.h"
#include "Skald_PlayerCharacter.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "TimerManager.h"
#include "UI/SkaldMainHUDWidget.h"
#include "WorldMap.h"

namespace {
constexpr int32 ExpectedPlayerCount = 4;
constexpr float StartGameTimeout = 10.f;
constexpr int32 StartingResources = 100;
constexpr int32 DefaultAIMaxCost = 10;
// Instance variables moved into ASkaldGameMode to avoid cross-instance
// interference; see header for declarations.
} // namespace

ASkaldGameMode::ASkaldGameMode() {
  GameStateClass = ASkaldGameState::StaticClass();
  PlayerStateClass = ASkaldPlayerState::StaticClass();
  PlayerControllerClass = ASkaldPlayerController::StaticClass();
  DefaultPawnClass = ASkald_PlayerCharacter::StaticClass();

  TurnManager = nullptr;
  WorldMap = nullptr;
  bTurnsStarted = false;
  bWorldInitialized = false;

  // Preallocate slots so blueprint scripts can safely write
  // player data to indices without hitting "invalid index" warnings.
  PlayerDataArray.SetNum(ExpectedPlayerCount);
  NextSiegeID = 1;
}

void ASkaldGameMode::BeginPlay() {
  Super::BeginPlay();

  if (!TurnManager) {
    TurnManager = GetWorld()->SpawnActor<ATurnManager>();
  }

  if (!WorldMap) {
    WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
        GetWorld(), AWorldMap::StaticClass()));
  }

  for (FConstPlayerControllerIterator It =
           GetWorld()->GetPlayerControllerIterator();
       It; ++It) {
    if (ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(*It)) {
      RegisterPlayer(PC);
    }
  }

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    if (GI->LoadedSaveGame) {
      ApplyLoadedGame(GI->LoadedSaveGame);
      GI->LoadedSaveGame = nullptr;
      return;
    }
  }

  PopulateAIPlayers();
  RefreshHUDs();

  TryInitializeWorldAndStart();

  // Auto-select fighters for AI players on the battle map
  const FString CurrentLevel =
      UGameplayStatics::GetCurrentLevelName(this, true);
  bool bIsBattleMap =
      CurrentLevel.Equals(TEXT("BattleMap"), ESearchCase::IgnoreCase);
  if (!bIsBattleMap && TurnManager) {
    for (const TSoftObjectPtr<UWorld> &Map : TurnManager->BattleMaps) {
      if (CurrentLevel.Equals(Map.ToSoftObjectPath().GetAssetName(),
                              ESearchCase::IgnoreCase)) {
        bIsBattleMap = true;
        break;
      }
    }
  }
  if (bIsBattleMap) {
    if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
      if (GI->GridBattleManager) {
        ASkaldGameState *GS = GetGameState<ASkaldGameState>();
        if (GS) {
          for (APlayerState *BasePS : GS->PlayerArray) {
            ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(BasePS);
            if (!PS || !PS->bIsAI) {
              continue;
            }

            TArray<FFighterDefinition> Definitions =
                GI->GridBattleManager->GetFightersForFaction(PS->Faction);
            if (Definitions.Num() <= 0) {
              PS->bHasLockedIn = true;
              continue;
            }

            const int32 MaxCost = GI->PendingBattle.ArmyCountSent > 0
                                      ? GI->PendingBattle.ArmyCountSent
                                      : DefaultAIMaxCost;
            int32 CurrentCost = 0;

            Algo::RandomShuffle(Definitions);

            TArray<FFighter> Fighters;
            for (const FFighterDefinition &Def : Definitions) {
              if (CurrentCost + Def.Stats.ArmyCost > MaxCost) {
                continue;
              }
              FFighter Fighter;
              Fighter.Stats = Def.Stats;
              Fighter.Faction = PS->Faction;
              Fighters.Add(Fighter);
              CurrentCost += Def.Stats.ArmyCost;
            }

            if (Fighters.Num() > 0) {
              GI->GridBattleManager->InitBattle(Fighters, Fighters);
              GI->GridBattleManager->RollInitiative();
              GI->GridBattleManager->StartRound(GI->CombatRandomStream);
            }

            PS->bHasLockedIn = true;
          }
        }
      }
    }
  }
}

void ASkaldGameMode::PostLogin(APlayerController *NewPlayer) {
  Super::PostLogin(NewPlayer);

  ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(NewPlayer);
  if (!PC) {
    return;
  }

  RegisterPlayer(PC);
  PopulateAIPlayers();
  RefreshHUDs();

  TryInitializeWorldAndStart();
}

void ASkaldGameMode::RegisterPlayer(ASkaldPlayerController *PC) {
  if (!PC) {
    return;
  }

  PendingControllers.AddUnique(PC);

  if (ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
    if (ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>()) {
      if (!GS->PlayerArray.Contains(PS)) {
        GS->AddPlayerState(PS);
      }

      if (PlayerDataArray.Num() < GS->PlayerArray.Num()) {
        PlayerDataArray.SetNum(GS->PlayerArray.Num());
      }

      if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
        if (!PS->bIsAI) {
          PS->PlayerDisplayName = GI->DisplayName;
          PS->Faction = GI->Faction;
        }
      }

      const int32 Index = GS->PlayerArray.IndexOfByKey(PS);
      if (PlayerDataArray.IsValidIndex(Index)) {
        PlayerDataArray[Index].PlayerID = PS->GetPlayerId();
        PlayerDataArray[Index].PlayerName = PS->PlayerDisplayName;
        PlayerDataArray[Index].IsAI = PS->bIsAI;
        PlayerDataArray[Index].Faction = PS->Faction;
        PlayerDataArray[Index].Resources = PS->Resources;
      }
    }
  }
}

void ASkaldGameMode::PopulateAIPlayers() {
  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GS || !GI || GI->bIsMultiplayer) {
    return;
  }

  // Guard against the unlikely scenario where player states fail to be
  // registered correctly and the loop below never makes progress. Without a
  // hard iteration cap the game can lock up while endlessly spawning AI
  // controllers during project startup.
  const int32 MaxSpawnAttempts = ExpectedPlayerCount * 2;
  int32 SpawnAttempts = 0;

  while (GS->PlayerArray.Num() < ExpectedPlayerCount &&
         SpawnAttempts++ < MaxSpawnAttempts) {
    ASkaldPlayerState *AIState =
        GetWorld()->SpawnActor<ASkaldPlayerState>(PlayerStateClass);
    if (!AIState) {
      break;
    }

    AIState->bIsAI = true;

    FTransform SpawnTransform = FTransform::Identity;
    ASkaldPlayerController *AIController =
        GetWorld()->SpawnActorDeferred<ASkaldPlayerController>(
            PlayerControllerClass, SpawnTransform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!AIController) {
      AIState->Destroy();
      break;
    }

    AIController->SetIsAIController(true);
    AIController->SetPlayerState(AIState);
    AIController->FinishSpawning(SpawnTransform);

    AIState->PlayerDisplayName =
        FString::Printf(TEXT("AI_%d"), GS->PlayerArray.Num());

    TArray<ESkaldFaction> Taken;
    for (APlayerState *ExistingPS : GS->PlayerArray) {
      if (ASkaldPlayerState *EPS = Cast<ASkaldPlayerState>(ExistingPS)) {
        Taken.Add(EPS->Faction);
      }
    }
    Taken.Append(GI->TakenFactions);
    TArray<ESkaldFaction> Available;
    if (UEnum *Enum = StaticEnum<ESkaldFaction>()) {
      for (int32 i = 0; i < Enum->NumEnums(); ++i) {
        if (Enum->HasMetaData(TEXT("Hidden"), i)) {
          continue;
        }
        ESkaldFaction Fac =
            static_cast<ESkaldFaction>(Enum->GetValueByIndex(i));
        if (Fac != ESkaldFaction::None && !Taken.Contains(Fac)) {
          Available.Add(Fac);
        }
      }
    }
    if (Available.Num() > 0) {
      FRandomStream RandStream;
      RandStream.Initialize(FMath::Rand());
      AIState->Faction =
          Available[RandStream.RandRange(0, Available.Num() - 1)];
      GI->TakenFactions.AddUnique(AIState->Faction);
    } else {
      UE_LOG(LogSkald, Error,
             TEXT("PopulateAIPlayers: no available factions for AI"));
      AIController->Destroy();
      AIState->Destroy();
      break;
    }

    AIState->bHasLockedIn = true;

    RegisterPlayer(AIController);

    if (!AIController->GetPawn() && DefaultPawnClass) {
      FActorSpawnParameters PawnParams;
      PawnParams.SpawnCollisionHandlingOverride =
          ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
      if (APawn *Pawn = GetWorld()->SpawnActor<APawn>(
              DefaultPawnClass, FVector::ZeroVector, FRotator::ZeroRotator,
              PawnParams)) {
        AIController->Possess(Pawn);
      }
    }

    HandlePlayerLockedIn(AIState);
  }

  if (GS->PlayerArray.Num() < ExpectedPlayerCount) {
    UE_LOG(
        LogSkald, Warning,
        TEXT("PopulateAIPlayers spawned only %d/%d players after %d attempts"),
        GS->PlayerArray.Num(), ExpectedPlayerCount, SpawnAttempts);
  }
}

void ASkaldGameMode::HandlePlayerLockedIn(ASkaldPlayerState *PS) {
  if (!PS) {
    return;
  }

  FS_PlayerData *PlayerData =
      PlayerDataArray.FindByPredicate([PS](const FS_PlayerData &Data) {
        return Data.PlayerID == PS->GetPlayerId();
      });
  if (PlayerData) {
    PlayerData->PlayerName = PS->PlayerDisplayName;
    PlayerData->Faction = PS->Faction;
  }

  // If a human player has locked in and there are still open slots,
  // populate them with AI players so the match can start.
  if (!PS->bIsAI) {
    PopulateAIPlayers();
  }

  RefreshHUDs();
  TryInitializeWorldAndStart();
}

void ASkaldGameMode::RefreshHUDs() {
  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    return;
  }

  TArray<FS_PlayerData> AllPlayers;
  for (APlayerState *PSBase : GS->PlayerArray) {
    if (ASkaldPlayerState *SPS = Cast<ASkaldPlayerState>(PSBase)) {
      FS_PlayerData Data;
      Data.PlayerID = SPS->GetPlayerId();
      Data.PlayerName = SPS->PlayerDisplayName;
      Data.IsAI = SPS->bIsAI;
      Data.Faction = SPS->Faction;
      Data.Resources = SPS->Resources;
      AllPlayers.Add(Data);
    }
  }

  for (FConstPlayerControllerIterator It =
           GetWorld()->GetPlayerControllerIterator();
       It; ++It) {
    if (ASkaldPlayerController *RefreshPC = Cast<ASkaldPlayerController>(*It)) {
      if (USkaldMainHUDWidget *HUD = RefreshPC->GetHUDWidget()) {
        HUD->RefreshPlayerList(AllPlayers);
      }
    }
  }
}

void ASkaldGameMode::UpdatePlayerResources(ASkaldPlayerState *Player) {
  if (!Player) {
    return;
  }

  FS_PlayerData *PlayerData =
      PlayerDataArray.FindByPredicate([Player](const FS_PlayerData &Data) {
        return Data.PlayerID == Player->GetPlayerId();
      });
  if (PlayerData) {
    PlayerData->Resources = Player->Resources;
  }
}

void ASkaldGameMode::TryInitializeWorldAndStart() {
  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    return;
  }

  bool bAllLockedIn = true;
  for (APlayerState *PSBase : GS->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      if (!PS->bHasLockedIn) {
        bAllLockedIn = false;
        break;
      }
    } else {
      bAllLockedIn = false;
      break;
    }
  }

  const bool bReadyToStart =
      bAllLockedIn && GS->PlayerArray.Num() == ExpectedPlayerCount;

  if (PendingControllers.Num() > 0) {
    for (ASkaldPlayerController *PC : PendingControllers) {
      if (TurnManager) {
        TurnManager->RegisterController(PC);
      }
    }
    PendingControllers.Empty();
  }

  if (!bWorldInitialized && bReadyToStart) {
    if (InitializeWorld()) {
      bWorldInitialized = true;
      BeginArmyPlacementPhase();
    }
  }

  if (bWorldInitialized && bReadyToStart && !bTurnsStarted && TurnManager &&
      TurnManager->GetCurrentPhase() != ETurnPhase::ArmyPlacement &&
      TurnManager->GetControllerCount() > 0) {
    bTurnsStarted = true;
    TurnManager->SortControllersByInitiative();
    TurnManager->StartTurns();

    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
                                       TEXT("Game started"));
    }
  }
}

void ASkaldGameMode::ApplyLoadedGame(USkaldSaveGame *LoadedGame) {
  if (!LoadedGame || !WorldMap) {
    return;
  }

  bWorldInitialized = true;
  bTurnsStarted = true;

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (GS) {
    GS->Players.Empty();
    GS->PlayerArray.Empty();
    GS->CurrentTurnIndex = LoadedGame->CurrentPlayerIndex;
  }

  PlayerDataArray.Empty();
  for (const FPlayerSaveStruct &PlayerSave : LoadedGame->Players) {
    ASkaldPlayerState *PS = GetWorld()->SpawnActor<ASkaldPlayerState>();
    if (!PS) {
      continue;
    }
    PS->SetPlayerId(PlayerSave.PlayerID);
    PS->PlayerDisplayName = PlayerSave.PlayerName;
    PS->bIsAI = PlayerSave.IsAI;
    PS->Faction = PlayerSave.Faction;
    PS->Resources = PlayerSave.Resources;
    if (GS) {
      GS->AddPlayerState(PS);
    }

    FS_PlayerData Data;
    Data.PlayerID = PlayerSave.PlayerID;
    Data.PlayerName = PlayerSave.PlayerName;
    Data.IsAI = PlayerSave.IsAI;
    Data.Faction = PlayerSave.Faction;
    Data.Resources = PlayerSave.Resources;
    Data.CapitalTerritoryIDs = PlayerSave.CapitalTerritoryIDs;
    Data.IsEliminated = PlayerSave.IsEliminated;
    PlayerDataArray.Add(Data);

    if (TurnManager) {
      TurnManager->BroadcastResources(PS);
    }
  }

  SiegePool = LoadedGame->Sieges;
  NextSiegeID = 1;
  for (const FS_Siege &S : SiegePool) {
    NextSiegeID = FMath::Max(NextSiegeID, S.SiegeID + 1);
  }

  for (const FS_Territory &TerrData : LoadedGame->Territories) {
    ATerritory *Territory = WorldMap->GetTerritoryById(TerrData.TerritoryID);
    if (!Territory) {
      continue;
    }

    ASkaldPlayerState *TerritoryOwner = nullptr;
    if (GS) {
      for (ASkaldPlayerState *PS : GS->Players) {
        if (PS && PS->GetPlayerId() == TerrData.OwnerPlayerID) {
          TerritoryOwner = PS;
          break;
        }
      }
    }

    Territory->OwningPlayer = TerritoryOwner;
    Territory->ArmyUnits = TerrData.ArmyUnits;
    Territory->bIsCapital = TerrData.IsCapital;
    Territory->ContinentID = TerrData.ContinentID;
    Territory->BuiltSiegeID = TerrData.BuiltSiegeID;
    Territory->SetActorLocation(TerrData.Location);
    Territory->RefreshAppearance();
  }

  if (APlayerController *PC = UGameplayStatics::GetPlayerController(this, 0)) {
    if (APawn *Pawn = PC->GetPawn()) {
      FVector Loc = Pawn->GetActorLocation();
      Loc.X = LoadedGame->SavedViewOffset.X;
      Loc.Y = LoadedGame->SavedViewOffset.Y;
      Pawn->SetActorLocation(Loc);

      if (UCameraComponent *Camera =
              Pawn->FindComponentByClass<UCameraComponent>()) {
        if (LoadedGame->SavedZoomAmount > 0.f) {
          Camera->SetFieldOfView(LoadedGame->SavedZoomAmount);
        }
      }
    }
  }

  RefreshHUDs();

  if (TurnManager) {
    TurnManager->SortControllersByInitiative();
    for (int32 i = 0; i < LoadedGame->CurrentPlayerIndex; ++i) {
      TurnManager->AdvanceTurn();
    }
  }
}

void ASkaldGameMode::BeginArmyPlacementPhase() {
  if (!TurnManager || !WorldMap) {
    return;
  }

  // Ensure controllers are sorted before placement begins and set phase.
  TurnManager->SortControllersByInitiative();
  TurnManager->StartArmyPlacementPhase();

  const TArray<ASkaldPlayerController *> Controllers =
      TurnManager->GetControllers();
  ASkaldPlayerController *ActiveController =
      Controllers.Num() > 0 ? Controllers[0] : nullptr;
  const FString PhaseString =
      UEnum::GetValueAsString(TurnManager->GetCurrentPhase());
  UE_LOG(LogSkald, Log, TEXT("BeginArmyPlacementPhase: Controller=%s Phase=%s"),
         *GetNameSafe(ActiveController), *PhaseString);
  if (GEngine) {
    GEngine->AddOnScreenDebugMessage(
        -1, 5.f, FColor::Green,
        FString::Printf(TEXT("Begin Army Placement: %s - %s"),
                        *GetNameSafe(ActiveController), *PhaseString));
  }

  // Calculate deployable units for each player based on owned territories and
  // update HUDs.
  for (ASkaldPlayerController *PC : TurnManager->GetControllers()) {
    if (ASkaldPlayerState *PS =
            PC ? PC->GetPlayerState<ASkaldPlayerState>() : nullptr) {
      int32 Owned = 0;
      for (ATerritory *Territory : WorldMap->Territories) {
        if (Territory && Territory->OwningPlayer == PS) {
          ++Owned;
        }
      }
      PS->DeployableUnits = FMath::CeilToInt(Owned / 3.f);
      TurnManager->BroadcastDeployableUnits(PS);
    }
  }

  PlacementIndex = -1;
  AdvanceArmyPlacement();
}

int32 ASkaldGameMode::BuildSiegeAtTerritory(int32 TerritoryID,
                                            ESiegeWeapon Type) {
  if (!WorldMap) {
    return 0;
  }
  ATerritory *Terr = WorldMap->GetTerritoryById(TerritoryID);
  if (!Terr || Terr->BuiltSiegeID != 0) {
    return 0;
  }
  FS_Siege NewSiege;
  NewSiege.SiegeID = NextSiegeID++;
  NewSiege.Type = Type;
  NewSiege.BuiltAtTerritoryID = TerritoryID;
  SiegePool.Add(NewSiege);
  Terr->BuiltSiegeID = NewSiege.SiegeID;
  return NewSiege.SiegeID;
}

int32 ASkaldGameMode::ConsumeSiege(int32 TerritoryID) {
  if (!WorldMap) {
    return 0;
  }
  ATerritory *Terr = WorldMap->GetTerritoryById(TerritoryID);
  if (!Terr || Terr->BuiltSiegeID == 0) {
    return 0;
  }
  const int32 SiegeID = Terr->BuiltSiegeID;
  Terr->BuiltSiegeID = 0;
  if (FS_Siege *Siege = SiegePool.FindByPredicate(
          [SiegeID](const FS_Siege &S) { return S.SiegeID == SiegeID; })) {
    Siege->AssignedToUnitID = TerritoryID;
  }
  return SiegeID;
}

void ASkaldGameMode::AdvanceArmyPlacement() {
  if (!TurnManager || !WorldMap) {
    return;
  }

  const TArray<ASkaldPlayerController *> Controllers =
      TurnManager->GetControllers();
  const int32 NumControllers = Controllers.Num();

  ++PlacementIndex;

  while (PlacementIndex < NumControllers) {
    ASkaldPlayerController *PC = Controllers[PlacementIndex];
    ASkaldPlayerState *PS =
        PC ? PC->GetPlayerState<ASkaldPlayerState>() : nullptr;
    if (!PC || !PS) {
      ++PlacementIndex;
      continue;
    }

    if (PS->DeployableUnits <= 0) {
      ++PlacementIndex;
      continue;
    }

    const FString PhaseString =
        UEnum::GetValueAsString(TurnManager->GetCurrentPhase());
    UE_LOG(LogSkald, Log, TEXT("AdvanceArmyPlacement: Controller=%s Phase=%s"),
           *GetNameSafe(PC), *PhaseString);
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Green,
          FString::Printf(TEXT("Army Placement: %s - %s"), *GetNameSafe(PC),
                          *PhaseString));
    }

    TurnManager->BroadcastDeployableUnits(PS);

    // Announce whose placement turn it is.
    const FString PlayerName = PS->PlayerDisplayName;
    for (ASkaldPlayerController *Controller : Controllers) {
      const bool bIsActive = Controller == PC;
      Controller->ShowTurnAnnouncement(PlayerName, bIsActive);
      if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
        HUD->UpdateTurnBanner(PS->GetPlayerId(), 1);
      }
    }

    // AI players automatically distribute their armies evenly.
    if (PS->bIsAI) {
      TArray<ATerritory *> OwnedTerritories;
      for (ATerritory *Territory : WorldMap->Territories) {
        if (Territory && Territory->OwningPlayer == PS) {
          OwnedTerritories.Add(Territory);
        }
      }
      int32 SpreadIndex = 0;
      while (PS->DeployableUnits > 0 && OwnedTerritories.Num() > 0) {
        ATerritory *TargetTerritory =
            OwnedTerritories[SpreadIndex % OwnedTerritories.Num()];
        ++TargetTerritory->ArmyUnits;
        TargetTerritory->RefreshAppearance();
        --PS->DeployableUnits;
        ++SpreadIndex;
      }
      TurnManager->BroadcastDeployableUnits(PS);
      ++PlacementIndex;
      continue;
    }

    // Human player: wait for manual deployment with current pool visible.
    return;
  }

  // All players have finished placing armies; start the main turn loop.
  bTurnsStarted = true;
  TurnManager->StartTurns();
}

bool ASkaldGameMode::InitializeWorld() {
  if (!WorldMap) {
    // Look for an existing world map actor placed in the level.
    WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
        GetWorld(), AWorldMap::StaticClass()));
  }
  if (!WorldMap) {
    UE_LOG(LogSkald, Error,
           TEXT("InitializeWorld failed: WorldMap missing in %s. Place a "
                "WorldMap actor in the level."),
           *GetName());
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          FString::Printf(TEXT("InitializeWorld: WorldMap missing in %s. Place "
                               "a WorldMap actor in the level."),
                          *GetName()));
    }
    return false;
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    UE_LOG(LogSkald, Error, TEXT("InitializeWorld failed: GameState missing"));
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red, TEXT("InitializeWorld: GameState missing"));
    }
    return false;
  }
  if (GS->PlayerArray.Num() == 0) {
    UE_LOG(LogSkald, Error, TEXT("InitializeWorld failed: no players"));
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red,
                                       TEXT("InitializeWorld: no players"));
    }
    return false;
  }

  if (WorldMap->Territories.Num() == 0) {
    if (!WorldMap->TerritoryClass) {
      UE_LOG(LogSkald, Error,
             TEXT("InitializeWorld failed: WorldMap %s missing TerritoryClass"),
             *WorldMap->GetName());
      if (GEngine) {
        GEngine->AddOnScreenDebugMessage(
            -1, 5.f, FColor::Red,
            FString::Printf(TEXT("InitializeWorld: %s missing TerritoryClass"),
                            *WorldMap->GetName()));
      }
      return false;
    }
    if (!WorldMap->TerritoryTable) {
      UE_LOG(LogSkald, Error,
             TEXT("InitializeWorld failed: WorldMap %s missing TerritoryTable"),
             *WorldMap->GetName());
      if (GEngine) {
        GEngine->AddOnScreenDebugMessage(
            -1, 5.f, FColor::Red,
            FString::Printf(TEXT("InitializeWorld: %s missing TerritoryTable"),
                            *WorldMap->GetName()));
      }
      return false;
    }
    if (!WorldMap->GenerateTerritoriesFromTable()) {
      UE_LOG(LogSkald, Error,
             TEXT("InitializeWorld failed: WorldMap %s could not generate "
                  "territories"),
             *WorldMap->GetName());
      if (GEngine) {
        GEngine->AddOnScreenDebugMessage(
            -1, 5.f, FColor::Red,
            FString::Printf(
                TEXT("InitializeWorld: %s could not generate territories"),
                *WorldMap->GetName()));
      }
      return false;
    }
  }
  if (WorldMap->Territories.Num() == 0) {
    UE_LOG(LogSkald, Error,
           TEXT("InitializeWorld failed: WorldMap %s has no territories"),
           *WorldMap->GetName());
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          FString::Printf(TEXT("InitializeWorld: %s has no territories"),
                          *WorldMap->GetName()));
    }
    return false;
  }
  // Shuffle territories before assignment
  Algo::RandomShuffle(WorldMap->Territories);

  // Roll initiative and sort players accordingly
  for (APlayerState *PSBase : GS->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
        PS->InitiativeRoll = GI->CombatRandomStream.RandRange(1, 6);
      } else {
        static FRandomStream FallbackStream;
        FallbackStream.Initialize(FMath::Rand());
        PS->InitiativeRoll = FallbackStream.RandRange(1, 6);
      }
    }
  }

  GS->PlayerArray.Sort([](const APlayerState &A, const APlayerState &B) {
    const ASkaldPlayerState *PSA = Cast<const ASkaldPlayerState>(&A);
    const ASkaldPlayerState *PSB = Cast<const ASkaldPlayerState>(&B);
    const int32 RollA = PSA ? PSA->InitiativeRoll : 0;
    const int32 RollB = PSB ? PSB->InitiativeRoll : 0;
    return RollA > RollB;
  });

  // Assign starting resources to each player
  for (APlayerState *PSBase : GS->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      PS->Resources = StartingResources;
      FS_PlayerData *PlayerData =
          PlayerDataArray.FindByPredicate([PS](const FS_PlayerData &Data) {
            return Data.PlayerID == PS->GetPlayerId();
          });
      if (PlayerData) {
        PlayerData->Resources = StartingResources;
      }
      if (TurnManager) {
        TurnManager->BroadcastResources(PS);
      }
    }
  }

  // Ensure the expected number of players are present before assigning territories
  const int32 PlayerCount = GS->PlayerArray.Num();
  if (PlayerCount != ExpectedPlayerCount) {
    UE_LOG(LogSkald, Warning,
           TEXT("InitializeWorld aborted: expected %d players but found %d"),
           ExpectedPlayerCount, PlayerCount);
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Yellow,
          FString::Printf(
              TEXT("InitializeWorld: expected %d players but found %d"),
              ExpectedPlayerCount, PlayerCount));
    }
    return false;
  }

  // Assign territories round-robin to players in initiative order
  int32 Index = 0;
  for (ATerritory *Territory : WorldMap->Territories) {
    if (Territory && PlayerCount > 0) {
      ASkaldPlayerState *TerritoryOwner =
          Cast<ASkaldPlayerState>(GS->PlayerArray[Index % PlayerCount]);
      Territory->OwningPlayer = TerritoryOwner;
      Territory->bIsCapital = false;
      Territory->ArmyUnits = 1;
      Territory->RefreshAppearance();
      ++Index;
    }
  }

  // Choose capitals for each player
  for (APlayerState *PSBase : GS->PlayerArray) {
    ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase);
    if (!PS) {
      continue;
    }

    TArray<ATerritory *> OwnedTerritories;
    for (ATerritory *Territory : WorldMap->Territories) {
      if (Territory && Territory->OwningPlayer == PS) {
        OwnedTerritories.Add(Territory);
      }
    }
    Algo::RandomShuffle(OwnedTerritories);

    FS_PlayerData *PlayerData =
        PlayerDataArray.FindByPredicate([PS](const FS_PlayerData &Data) {
          return Data.PlayerID == PS->GetPlayerId();
        });
    if (PlayerData) {
      PlayerData->CapitalTerritoryIDs.Reset();
    }

    int32 CapitalsAssigned = 0;
    for (ATerritory *Territory : OwnedTerritories) {
      if (CapitalsAssigned >= 2) {
        break;
      }
      Territory->bIsCapital = true;
      Territory->RefreshAppearance();
      if (PlayerData) {
        PlayerData->CapitalTerritoryIDs.Add(Territory->TerritoryID);
      }
      ++CapitalsAssigned;
    }
  }

  // Determine highest initiative roll
  ASkaldPlayerState *HighestPS = nullptr;
  int32 HighestRoll = 0;
  for (APlayerState *PSBase : GS->PlayerArray) {
    ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase);
    if (!PS) {
      continue;
    }

    if (PS->InitiativeRoll > HighestRoll) {
      HighestRoll = PS->InitiativeRoll;
      HighestPS = PS;
    }
  }

  if (HighestPS) {
    const FString Message =
        FString::Printf(TEXT("%s wins initiative with a roll of %d"),
                        *HighestPS->PlayerDisplayName, HighestRoll);
    for (FConstPlayerControllerIterator It =
             GetWorld()->GetPlayerControllerIterator();
         It; ++It) {
      if (ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(*It)) {
        if (USkaldMainHUDWidget *HUD = PC->GetHUDWidget()) {
          HUD->UpdateInitiativeText(Message);
        } else {
          UE_LOG(LogSkald, Warning,
                 TEXT("InitializeWorld: Controller %s missing HUD widget"),
                 *PC->GetName());
          if (GEngine) {
            GEngine->AddOnScreenDebugMessage(
                -1, 5.f, FColor::Yellow,
                FString::Printf(TEXT("No HUD for %s"), *PC->GetName()));
          }
        }
      }
    }
  }
  return true;
}

void ASkaldGameMode::FillSaveGame(USkaldSaveGame *SaveGameObject) const {
  if (!SaveGameObject) {
    return;
  }

  // Store basic turn information.
  SaveGameObject->TurnNumber = 0; // Turn tracking not yet implemented
  if (const ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
    SaveGameObject->CurrentPlayerIndex = GS->CurrentTurnIndex;
  }

  // Preserve current camera position and zoom so the view can be restored
  // when the game is loaded again.
  if (APlayerController *PC = UGameplayStatics::GetPlayerController(this, 0)) {
    if (APawn *Pawn = PC->GetPawn()) {
      const FVector Loc = Pawn->GetActorLocation();
      SaveGameObject->SavedViewOffset = FVector2D(Loc.X, Loc.Y);

      if (UCameraComponent *Camera =
              Pawn->FindComponentByClass<UCameraComponent>()) {
        SaveGameObject->SavedZoomAmount = Camera->FieldOfView;
      }
    }
  }

  // Copy player data.
  SaveGameObject->Players.Empty();
  for (const FS_PlayerData &Data : PlayerDataArray) {
    FPlayerSaveStruct PlayerSave;
    PlayerSave.PlayerID = Data.PlayerID;
    PlayerSave.PlayerName = Data.PlayerName;
    PlayerSave.IsAI = Data.IsAI;
    PlayerSave.Faction = Data.Faction;
    PlayerSave.Resources = Data.Resources;
    PlayerSave.CapitalTerritoryIDs = Data.CapitalTerritoryIDs;
    PlayerSave.IsEliminated = Data.IsEliminated;
    SaveGameObject->Players.Add(PlayerSave);
  }

  // Capture current territory state.
  SaveGameObject->Territories.Empty();
  if (WorldMap) {
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
      for (ATerritory *Adj : Territory->AdjacentTerritories) {
        if (Adj) {
          TerrData.AdjacentIDs.Add(Adj->TerritoryID);
        }
      }
      TerrData.Location = Territory->GetActorLocation();
      TerrData.BuiltSiegeID = Territory->BuiltSiegeID;
      SaveGameObject->Territories.Add(TerrData);
    }
  }

  // Store current siege equipment state.
  SaveGameObject->Sieges = SiegePool;
}

void ASkaldGameMode::CheckVictoryConditions() {
  if (!WorldMap) {
    return;
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    return;
  }

  int32 RemainingPlayers = 0;
  ASkaldPlayerState *WinningPlayer = nullptr;

  for (APlayerState *PSBase : GS->PlayerArray) {
    ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase);
    if (!PS) {
      continue;
    }

    bool bHasTerritory = false;
    for (ATerritory *Territory : WorldMap->Territories) {
      if (Territory && Territory->OwningPlayer == PS) {
        bHasTerritory = true;
        break;
      }
    }

    FS_PlayerData *Data =
        PlayerDataArray.FindByPredicate([PS](const FS_PlayerData &D) {
          return D.PlayerID == PS->GetPlayerId();
        });
    if (Data) {
      Data->IsEliminated = !bHasTerritory;
    }

    if (bHasTerritory) {
      ++RemainingPlayers;
      WinningPlayer = PS;
    }
  }

  if (RemainingPlayers == 1 && WinningPlayer) {
    OnGameOver.Broadcast(WinningPlayer);
    if (UWorld *WorldToTravel = GetWorld()) {
      WorldToTravel->ServerTravel(TEXT("EndScreen"));
    }
  }
}
