#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Engine/World.h"

ASkaldGameMode::ASkaldGameMode()
{
    GameStateClass = ASkaldGameState::StaticClass();
    PlayerControllerClass = ASkaldPlayerController::StaticClass();
    PlayerStateClass = ASkaldPlayerState::StaticClass();
    TurnManager = nullptr;
}

void ASkaldGameMode::BeginPlay()
{
    Super::BeginPlay();

    if (!TurnManager)
    {
        TurnManager = GetWorld()->SpawnActor<ATurnManager>();
    }
}

void ASkaldGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    ASkaldPlayerController* PC = Cast<ASkaldPlayerController>(NewPlayer);
    if (TurnManager)
    {
        TurnManager->RegisterController(PC);
    }

    ASkaldGameState* GS = GetGameState<ASkaldGameState>();
    if (GS)
    {
        GS->AddPlayerState(Cast<ASkaldPlayerState>(PC->PlayerState));
    }
}

