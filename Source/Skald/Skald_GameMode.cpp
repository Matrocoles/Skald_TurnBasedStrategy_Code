#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "WorldMap.h"
#include "Engine/World.h"

ASkaldGameMode::ASkaldGameMode()
{
    GameStateClass = ASkaldGameState::StaticClass();
    PlayerControllerClass = ASkaldPlayerController::StaticClass();
    PlayerStateClass = ASkaldPlayerState::StaticClass();
    TurnManager = nullptr;
    WorldMap = nullptr;
}

void ASkaldGameMode::BeginPlay()
{
    Super::BeginPlay();

    if (!TurnManager)
    {
        TurnManager = GetWorld()->SpawnActor<ATurnManager>();
    }

    if (!WorldMap)
    {
        WorldMap = GetWorld()->SpawnActor<AWorldMap>();
    }
}

void ASkaldGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    ASkaldPlayerController* PC = Cast<ASkaldPlayerController>(NewPlayer);
    if (PC)
    {
        if (TurnManager)
        {
            TurnManager->RegisterController(PC);
        }

        if (ASkaldGameState* GS = GetGameState<ASkaldGameState>())
        {
            if (ASkaldPlayerState* PS = PC->GetPlayerState<ASkaldPlayerState>())
            {
                GS->AddPlayerState(PS);
            }
        }
    }
}

