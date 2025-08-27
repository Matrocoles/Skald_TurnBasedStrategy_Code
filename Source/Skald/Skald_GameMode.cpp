#include "Skald_GameMode.h"
#include "Algo/RandomShuffle.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
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
#include "Camera/CameraComponent.h"

namespace {
constexpr int32 ExpectedPlayerCount = 4;
constexpr float StartGameTimeout = 10.f;
constexpr int32 StartingResources = 100;
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
  PlayersData.SetNum(ExpectedPlayerCount);
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
    if (!WorldMap) {
      WorldMap = GetWorld()->SpawnActor<AWorldMap>();
    }
  }

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    if (GI->LoadedSaveGame) {
      ApplyLoadedGame(GI->LoadedSaveGame);
      GI->LoadedSaveGame = nullptr;
      return;
    }
  }

  TryInitializeWorldAndStart();
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
  if (TurnManager) {
    TurnManager->RegisterController(PC);
  }

  if (ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
    if (ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>()) {
      GS->AddPlayerState(PS);

      if (PlayersData.Num() < GS->PlayerArray.Num()) {
        PlayersData.SetNum(GS->PlayerArray.Num());
      }

      if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
        PS->DisplayName = GI->DisplayName;
        PS->Faction = GI->Faction;
      }

      const int32 Index = GS->PlayerArray.IndexOfByKey(PS);
      if (PlayersData.IsValidIndex(Index)) {
        PlayersData[Index].PlayerID = PS->GetPlayerId();
        PlayersData[Index].PlayerName = PS->DisplayName;
        PlayersData[Index].IsAI = PS->bIsAI;
        PlayersData[Index].Faction = PS->Faction;
        PlayersData[Index].Resources = PS->Resources;
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

  while (GS->PlayerArray.Num() < ExpectedPlayerCount) {
    ASkaldPlayerState *AIState =
        GetWorld()->SpawnActor<ASkaldPlayerState>(PlayerStateClass);
    if (!AIState) {
      break;
    }
    AIState->bIsAI = true;
    AIState->DisplayName =
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
      AIState->Faction = Available[RandStream.RandRange(0, Available.Num() - 1)];
      GI->TakenFactions.AddUnique(AIState->Faction);
    }

    GS->AddPlayerState(AIState);

    if (PlayersData.Num() < GS->PlayerArray.Num()) {
      PlayersData.SetNum(GS->PlayerArray.Num());
    }
    const int32 Index = GS->PlayerArray.Num() - 1;
    PlayersData[Index].PlayerID = AIState->GetPlayerId();
    PlayersData[Index].PlayerName = AIState->DisplayName;
    PlayersData[Index].IsAI = true;
    PlayersData[Index].Faction = AIState->Faction;
    PlayersData[Index].Resources = AIState->Resources;
  }
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
      Data.PlayerName = SPS->DisplayName;
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
      PlayersData.FindByPredicate([Player](const FS_PlayerData &Data) {
        return Data.PlayerID == Player->GetPlayerId();
      });
  if (PlayerData) {
    PlayerData->Resources = Player->Resources;
  }
}

void ASkaldGameMode::TryInitializeWorldAndStart() {
  const bool bWasInitialized = bWorldInitialized;

  if (!bWorldInitialized && InitializeWorld()) {
    bWorldInitialized = true;
    BeginArmyPlacementPhase();
  }

  if (ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
    const bool bReadyToStart =
        GS->PlayerArray.Num() >= ExpectedPlayerCount;
    const bool bHaveControllers =
        TurnManager && TurnManager->GetControllerCount() > 0;
    if (bWorldInitialized && bReadyToStart && !bTurnsStarted && bHaveControllers) {
      bTurnsStarted = true;
      TurnManager->SortControllersByInitiative();
      TurnManager->StartTurns();

      if (GEngine) {
        GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
                                         TEXT("Game started"));
      }
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

  PlayersData.Empty();
  for (const FPlayerSaveStruct &PlayerSave : LoadedGame->Players) {
    ASkaldPlayerState *PS = GetWorld()->SpawnActor<ASkaldPlayerState>();
    if (!PS) {
      continue;
    }
    PS->SetPlayerId(PlayerSave.PlayerID);
    PS->DisplayName = PlayerSave.PlayerName;
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
    PlayersData.Add(Data);

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
    Territory->ArmyStrength = TerrData.ArmyCount;
    Territory->bIsCapital = TerrData.IsCapital;
    Territory->ContinentID = TerrData.ContinentID;
    Territory->BuiltSiegeID = TerrData.BuiltSiegeID;
    Territory->SetActorLocation(TerrData.Location);
    Territory->RefreshAppearance();
    Territory->ForceNetUpdate();
  }

  if (APlayerController *PC = UGameplayStatics::GetPlayerController(this, 0)) {
    if (APawn *Pawn = PC->GetPawn()) {
      FVector Loc = Pawn->GetActorLocation();
      Loc.X = LoadedGame->SavedViewOffset.X;
      Loc.Y = LoadedGame->SavedViewOffset.Y;
      Pawn->SetActorLocation(Loc);

      if (UCameraComponent *Camera = Pawn->FindComponentByClass<UCameraComponent>()) {
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

  // Ensure controllers are sorted before placement begins.
  TurnManager->SortControllersByInitiative();

  // Calculate army pools for each player based on owned territories.
  for (ASkaldPlayerController* PC : TurnManager->GetControllers()) {
    if (ASkaldPlayerState* PS = PC ? PC->GetPlayerState<ASkaldPlayerState>() : nullptr) {
      int32 Owned = 0;
      for (ATerritory* Territory : WorldMap->Territories) {
        if (Territory && Territory->OwningPlayer == PS) {
          ++Owned;
        }
      }
      PS->ArmyPool = FMath::CeilToInt(Owned / 3.0f);
      PS->ForceNetUpdate();
    }
  }

  PlacementIndex = 0;
  AdvanceArmyPlacement();
}

int32 ASkaldGameMode::BuildSiegeAtTerritory(int32 TerritoryID,
                                            E_SiegeWeapons Type) {
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
  Terr->ForceNetUpdate();
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
  Terr->ForceNetUpdate();
  return SiegeID;
}

void ASkaldGameMode::AdvanceArmyPlacement() {
  if (!TurnManager || !WorldMap) {
    return;
  }

  const TArray<ASkaldPlayerController*> Controllers = TurnManager->GetControllers();
  const int32 NumControllers = Controllers.Num();

  while (PlacementIndex < NumControllers) {
    ASkaldPlayerController* PC = Controllers[PlacementIndex];
    ASkaldPlayerState* PS = PC ? PC->GetPlayerState<ASkaldPlayerState>() : nullptr;
    if (!PC || !PS) {
      ++PlacementIndex;
      continue;
    }

    if (PS->ArmyPool <= 0) {
      ++PlacementIndex;
      continue;
    }

    if (TurnManager) {
      TurnManager->BroadcastArmyPool(PS);
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
      while (PS->ArmyPool > 0 && OwnedTerritories.Num() > 0) {
        ATerritory *TargetTerritory =
            OwnedTerritories[SpreadIndex % OwnedTerritories.Num()];
        ++TargetTerritory->ArmyStrength;
        TargetTerritory->RefreshAppearance();
        --PS->ArmyPool;
        ++SpreadIndex;
      }
      PS->ForceNetUpdate();
      if (TurnManager) {
        TurnManager->BroadcastArmyPool(PS);
      }
      ++PlacementIndex;
      continue;
    }

    // Human player: wait for manual deployment with current pool visible.
    break;
  }
}

bool ASkaldGameMode::InitializeWorld() {
  if (!WorldMap) {
    // Try to locate an existing world map actor in the level.
    WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
        GetWorld(), AWorldMap::StaticClass()));
    // If none exists, spawn a default instance so setup can continue.
    if (!WorldMap) {
      WorldMap = GetWorld()->SpawnActor<AWorldMap>();
    }
  }
  if (!WorldMap) {
    UE_LOG(LogSkald, Error,
           TEXT("InitializeWorld failed: WorldMap missing in %s"),
           *GetName());
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          FString::Printf(TEXT("InitializeWorld: WorldMap missing in %s"),
                          *GetName()));
    }
    return false;
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    UE_LOG(LogSkald, Error, TEXT("InitializeWorld failed: GameState missing"));
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          TEXT("InitializeWorld: GameState missing"));
    }
    return false;
  }
  if (GS->PlayerArray.Num() == 0) {
    UE_LOG(LogSkald, Error, TEXT("InitializeWorld failed: no players"));
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          TEXT("InitializeWorld: no players"));
    }
    return false;
  }

  // If the world map has not spawned territories yet, create basic ones.
  if (WorldMap->Territories.Num() == 0) {
    for (int32 Id = 0; Id < 43; ++Id) {
      ATerritory *Territory = GetWorld()->SpawnActor<ATerritory>();
      if (Territory) {
        Territory->TerritoryID = Id;
        WorldMap->RegisterTerritory(Territory);
      }
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
          PlayersData.FindByPredicate([PS](const FS_PlayerData &Data) {
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

  // Assign territories round-robin to players in initiative order
  const int32 PlayerCount = GS->PlayerArray.Num();
  int32 Index = 0;
  for (ATerritory *Territory : WorldMap->Territories) {
    if (Territory && PlayerCount > 0) {
      ASkaldPlayerState *TerritoryOwner =
          Cast<ASkaldPlayerState>(GS->PlayerArray[Index % PlayerCount]);
      Territory->OwningPlayer = TerritoryOwner;
      Territory->bIsCapital = false;
      Territory->ArmyStrength = 1;
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
        PlayersData.FindByPredicate([PS](const FS_PlayerData &Data) {
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
                        *HighestPS->DisplayName, HighestRoll);
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
  for (const FS_PlayerData &Data : PlayersData) {
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
      TerrData.ArmyCount = Territory->ArmyStrength;
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

    FS_PlayerData *Data = PlayersData.FindByPredicate(
        [PS](const FS_PlayerData &D) { return D.PlayerID == PS->GetPlayerId(); });
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
    if (UWorld* WorldToTravel = GetWorld()) {
      WorldToTravel->ServerTravel(TEXT("EndScreen"));
    }
  }
}
