#include "Skald_GameMode.h"
#include "Algo/RandomShuffle.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
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

  TryInitializeWorldAndStart();
}

void ASkaldGameMode::PostLogin(APlayerController *NewPlayer) {
  Super::PostLogin(NewPlayer);

  ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(NewPlayer);
  if (PC) {
    if (TurnManager) {
      TurnManager->RegisterController(PC);
    }

    if (ASkaldGameState *GS = GetGameState<ASkaldGameState>()) {
      if (ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>()) {
        GS->AddPlayerState(PS);

        // Ensure PlayersData can hold at least as many entries as there
        // are connected players. Never shrink to avoid index issues in
        // existing blueprint logic.
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
        }
      }

      // In singleplayer fill remaining slots with AI opponents
      if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
        if (!GI->bIsMultiplayer) {
          // Populate AI players up to the expected count
          while (GS->PlayerArray.Num() < ExpectedPlayerCount) {
            ASkaldPlayerState *AIState =
                GetWorld()->SpawnActor<ASkaldPlayerState>(PlayerStateClass);
            if (!AIState) {
              break;
            }
            AIState->bIsAI = true;
            AIState->DisplayName =
                FString::Printf(TEXT("AI_%d"), GS->PlayerArray.Num());

            // Choose a faction not already taken
            TArray<ESkaldFaction> Taken;
            for (APlayerState *ExistingPS : GS->PlayerArray) {
              if (ASkaldPlayerState *EPS =
                      Cast<ASkaldPlayerState>(ExistingPS)) {
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
              AIState->Faction =
                  Available[FMath::RandRange(0, Available.Num() - 1)];
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
          }

          // Refresh HUDs with the updated player list
          TArray<FS_PlayerData> AllPlayers;
          for (APlayerState *PSBase : GS->PlayerArray) {
            if (ASkaldPlayerState *SPS = Cast<ASkaldPlayerState>(PSBase)) {
              FS_PlayerData Data;
              Data.PlayerID = SPS->GetPlayerId();
              Data.PlayerName = SPS->DisplayName;
              Data.IsAI = SPS->bIsAI;
              Data.Faction = SPS->Faction;
              AllPlayers.Add(Data);
            }
          }
          for (FConstPlayerControllerIterator It =
                   GetWorld()->GetPlayerControllerIterator();
               It; ++It) {
            if (ASkaldPlayerController *RefreshPC =
                    Cast<ASkaldPlayerController>(*It)) {
              if (USkaldMainHUDWidget *HUD = RefreshPC->GetHUDWidget()) {
                HUD->RefreshPlayerList(AllPlayers);
              }
            }
          }
        }
      }

      TryInitializeWorldAndStart();

      if (GS->PlayerArray.Num() >= ExpectedPlayerCount && !bTurnsStarted) {
        bTurnsStarted = true;
        GetWorldTimerManager().ClearTimer(StartGameTimerHandle);
        if (TurnManager) {
          TurnManager->SortControllersByInitiative();
          TurnManager->StartTurns();
          if (GEngine) {
            GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
                                             TEXT("Game started"));
          }
        }
      }
    }
  }
}

void ASkaldGameMode::TryInitializeWorldAndStart() {
  if (bWorldInitialized) {
    return;
  }

  if (InitializeWorld()) {
    bWorldInitialized = true;
    BeginArmyPlacementPhase();
    GetWorldTimerManager().SetTimer(
        StartGameTimerHandle, FTimerDelegate::CreateLambda([this]() {
          if (!bTurnsStarted && TurnManager) {
            bTurnsStarted = true;
            TurnManager->SortControllersByInitiative();
            TurnManager->StartTurns();
            if (GEngine) {
              GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
                                               TEXT("Game started"));
            }
          }
        }),
        StartGameTimeout, false);
  }
}

void ASkaldGameMode::BeginArmyPlacementPhase() {
  if (!TurnManager || !WorldMap) {
    return;
  }

  // Ensure controllers are sorted before placement begins.
  TurnManager->SortControllersByInitiative();

  // Calculate army pools for each player based on owned territories.
  for (ASkaldPlayerController *PC : TurnManager->GetControllers()) {
    if (!PC) {
      continue;
    }
    if (ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>()) {
      int32 Owned = 0;
      for (ATerritory *Territory : WorldMap->Territories) {
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

void ASkaldGameMode::AdvanceArmyPlacement() {
  if (!TurnManager || !WorldMap) {
    return;
  }

  const TArray<ASkaldPlayerController *> &Controllers =
      TurnManager->GetControllers();
  const int32 NumControllers = Controllers.Num();

  while (PlacementIndex < NumControllers) {
    ASkaldPlayerController *PC = Controllers[PlacementIndex];
    ASkaldPlayerState *PS = PC ? PC->GetPlayerState<ASkaldPlayerState>() : nullptr;
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
    return false;
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS || GS->PlayerArray.Num() == 0) {
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
  // Shuffle territories before assignment
  Algo::RandomShuffle(WorldMap->Territories);

  // Roll initiative and sort players accordingly
  for (APlayerState *PSBase : GS->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      PS->InitiativeRoll = FMath::RandRange(1, 6);
    }
  }

  GS->PlayerArray.Sort([](const APlayerState &A, const APlayerState &B) {
    const ASkaldPlayerState *PSA = Cast<const ASkaldPlayerState>(&A);
    const ASkaldPlayerState *PSB = Cast<const ASkaldPlayerState>(&B);
    const int32 RollA = PSA ? PSA->InitiativeRoll : 0;
    const int32 RollB = PSB ? PSB->InitiativeRoll : 0;
    return RollA > RollB;
  });

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
        }
      }
    }
  }
  return true;
}
