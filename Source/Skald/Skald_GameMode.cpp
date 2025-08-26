#include "Skald_GameMode.h"
#include "Engine/World.h"
#include "Skald_GameState.h"
#include "Skald_PlayerCharacter.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "TimerManager.h"
#include "WorldMap.h"
#include "Skald_GameInstance.h"
#include "UI/SkaldMainHUDWidget.h"

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
    WorldMap = GetWorld()->SpawnActor<AWorldMap>();
  }

  // Initialization of the world occurs after players join in PostLogin.
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

        if (USkaldGameInstance* GI = GetGameInstance<USkaldGameInstance>()) {
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
      if (USkaldGameInstance* GI = GetGameInstance<USkaldGameInstance>())
      {
        if (!GI->bIsMultiplayer)
        {
          // Populate AI players up to the expected count
          while (GS->PlayerArray.Num() < ExpectedPlayerCount)
          {
            ASkaldPlayerState* AIState = GetWorld()->SpawnActor<ASkaldPlayerState>(PlayerStateClass);
            if (!AIState)
            {
              break;
            }
            AIState->bIsAI = true;
            AIState->DisplayName = FString::Printf(TEXT("AI_%d"), GS->PlayerArray.Num());

            // Choose a faction not already taken
            TArray<ESkaldFaction> Taken;
            for (APlayerState* ExistingPS : GS->PlayerArray)
            {
              if (ASkaldPlayerState* EPS = Cast<ASkaldPlayerState>(ExistingPS))
              {
                Taken.Add(EPS->Faction);
              }
            }
            Taken.Append(GI->TakenFactions);
            TArray<ESkaldFaction> Available;
            if (UEnum* Enum = StaticEnum<ESkaldFaction>())
            {
              for (int32 i = 0; i < Enum->NumEnums(); ++i)
              {
                if (Enum->HasMetaData(TEXT("Hidden"), i))
                {
                  continue;
                }
                ESkaldFaction Fac = static_cast<ESkaldFaction>(Enum->GetValueByIndex(i));
                if (Fac != ESkaldFaction::None && !Taken.Contains(Fac))
                {
                  Available.Add(Fac);
                }
              }
            }
            if (Available.Num() > 0)
            {
              AIState->Faction = Available[FMath::RandRange(0, Available.Num() - 1)];
              GI->TakenFactions.AddUnique(AIState->Faction);
            }

            GS->AddPlayerState(AIState);

            if (PlayersData.Num() < GS->PlayerArray.Num())
            {
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
          for (APlayerState* PSBase : GS->PlayerArray)
          {
            if (ASkaldPlayerState* SPS = Cast<ASkaldPlayerState>(PSBase))
            {
              FS_PlayerData Data;
              Data.PlayerID = SPS->GetPlayerId();
              Data.PlayerName = SPS->DisplayName;
              Data.IsAI = SPS->bIsAI;
              Data.Faction = SPS->Faction;
              AllPlayers.Add(Data);
            }
          }
          for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
          {
            if (ASkaldPlayerController* RefreshPC = Cast<ASkaldPlayerController>(*It))
            {
              if (USkaldMainHUDWidget* HUD = RefreshPC->GetHUDWidget())
              {
                HUD->RefreshPlayerList(AllPlayers);
              }
            }
          }
        }
      }

      if (!bWorldInitialized)
      {
        InitializeWorld();
        BeginArmyPlacementPhase();
        bWorldInitialized = true;
        GetWorldTimerManager().SetTimer(
            StartGameTimerHandle, FTimerDelegate::CreateLambda([this]() {
              if (!bTurnsStarted && TurnManager) {
                bTurnsStarted = true;
                TurnManager->SortControllersByInitiative();
                TurnManager->StartTurns();
                if (GEngine)
                {
                  GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green, TEXT("Game started"));
                }
              }
            }),
            StartGameTimeout, false);
      }

      if (GS->PlayerArray.Num() >= ExpectedPlayerCount && !bTurnsStarted) {
        bTurnsStarted = true;
        GetWorldTimerManager().ClearTimer(StartGameTimerHandle);
        if (TurnManager) {
          TurnManager->SortControllersByInitiative();
          TurnManager->StartTurns();
          if (GEngine)
          {
            GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green, TEXT("Game started"));
          }
        }
      }
    }
  }
}

void ASkaldGameMode::BeginArmyPlacementPhase() {
  if (TurnManager) {
    // Initiative order determines who places armies first. The actual
    // placement UI is expected to be handled in blueprints.
    TurnManager->SortControllersByInitiative();
  }
}

void ASkaldGameMode::InitializeWorld() {
  if (!WorldMap) {
    return;
  }

  // If the world map has not spawned territories yet, create basic ones.
  if (WorldMap->Territories.Num() == 0)
  {
    for (int32 Id = 0; Id < 43; ++Id)
    {
      ATerritory *Territory = GetWorld()->SpawnActor<ATerritory>();
      if (Territory)
      {
        Territory->TerritoryID = Id;
        WorldMap->RegisterTerritory(Territory);
      }
    }
  }

  ASkaldGameState *GS = GetGameState<ASkaldGameState>();
  if (!GS) {
    return;
  }

  // Assign territories round-robin to players
  const int32 PlayerCount = GS->PlayerArray.Num();
  int32 Index = 0;
  for (ATerritory *Territory : WorldMap->Territories) {
    if (Territory && PlayerCount > 0) {
      // Rename local variable to avoid hiding AActor::Owner
      ASkaldPlayerState *TerritoryOwner =
          Cast<ASkaldPlayerState>(GS->PlayerArray[Index % PlayerCount]);
      Territory->OwningPlayer = TerritoryOwner;
      Territory->RefreshAppearance();
      Territory->ArmyStrength = 1;
      ++Index;
    }
  }

  // Calculate starting armies and initiative rolls
  for (APlayerState *PSBase : GS->PlayerArray) {
    ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase);
    if (!PS) {
      continue;
    }

    int32 Owned = 0;
    for (ATerritory *Territory : WorldMap->Territories) {
      if (Territory && Territory->OwningPlayer == PS) {
        ++Owned;
      }
    }

    PS->ArmyPool = Owned / 3;
    PS->InitiativeRoll = FMath::RandRange(1, 6);
  }
}
