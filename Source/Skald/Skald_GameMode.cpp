#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "WorldMap.h"
#include "Territory.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
    constexpr int32 ExpectedPlayerCount = 2;
    constexpr float StartGameTimeout = 10.f;
    FTimerHandle StartGameTimerHandle;
    bool bTurnsStarted = false;
}

ASkaldGameMode::ASkaldGameMode()
{
    GameStateClass = ASkaldGameState::StaticClass();
    PlayerControllerClass = ASkaldPlayerController::StaticClass();
    PlayerStateClass = ASkaldPlayerState::StaticClass();
    TurnManager = nullptr;
    WorldMap = nullptr;

    // Preallocate two slots so blueprint scripts can safely write
    // player data to indices without hitting "invalid index" warnings.
    PlayersData.SetNum(2);
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

    InitializeWorld();

    GetWorldTimerManager().SetTimer(
        StartGameTimerHandle,
        FTimerDelegate::CreateLambda([this]()
        {
            if (!bTurnsStarted && TurnManager)
            {
                bTurnsStarted = true;
                TurnManager->SortControllersByInitiative();
                TurnManager->StartTurns();
            }
        }),
        StartGameTimeout,
        false);
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

                // Ensure PlayersData can hold at least as many entries as there
                // are connected players. Never shrink to avoid index issues in
                // existing blueprint logic.
                if (PlayersData.Num() < GS->PlayerArray.Num())
                {
                    PlayersData.SetNum(GS->PlayerArray.Num());
                }
            }

            if (GS->PlayerArray.Num() >= ExpectedPlayerCount && !bTurnsStarted)
            {
                bTurnsStarted = true;
                GetWorldTimerManager().ClearTimer(StartGameTimerHandle);
                if (TurnManager)
                {
                    TurnManager->SortControllersByInitiative();
                    TurnManager->StartTurns();
                }
            }
        }
    }
}

void ASkaldGameMode::InitializeWorld()
{
    if (!WorldMap)
    {
        return;
    }

    // Spawn 43 territories with unique identifiers
    for (int32 Id = 0; Id < 43; ++Id)
    {
        ATerritory* Territory = GetWorld()->SpawnActor<ATerritory>();
        if (Territory)
        {
            Territory->TerritoryID = Id;
            WorldMap->RegisterTerritory(Territory);
        }
    }

    ASkaldGameState* GS = GetGameState<ASkaldGameState>();
    if (!GS)
    {
        return;
    }

    // Assign territories round-robin to players
    const int32 PlayerCount = GS->PlayerArray.Num();
    int32 Index = 0;
    for (ATerritory* Territory : WorldMap->Territories)
    {
        if (Territory && PlayerCount > 0)
        {
            // Rename local variable to avoid hiding AActor::Owner
            ASkaldPlayerState* TerritoryOwner = Cast<ASkaldPlayerState>(GS->PlayerArray[Index % PlayerCount]);
            Territory->OwningPlayer = TerritoryOwner;
            Territory->ArmyStrength = 1;
            ++Index;
        }
    }

    // Calculate starting armies and initiative rolls
    for (APlayerState* PSBase : GS->PlayerArray)
    {
        ASkaldPlayerState* PS = Cast<ASkaldPlayerState>(PSBase);
        if (!PS)
        {
            continue;
        }

        int32 Owned = 0;
        for (ATerritory* Territory : WorldMap->Territories)
        {
            if (Territory && Territory->OwningPlayer == PS)
            {
                ++Owned;
            }
        }

        PS->ArmyPool = Owned / 3;
        PS->InitiativeRoll = FMath::RandRange(1, 6);
    }

}

