#include "Skald_GameState.h"
#include "Skald_PlayerState.h"

ASkaldGameState::ASkaldGameState()
    : CurrentTurnIndex(0)
{
}

void ASkaldGameState::AddPlayerState(APlayerState* PlayerState)
{
    Super::AddPlayerState(PlayerState);

    if (ASkaldPlayerState* SkaldPlayer = Cast<ASkaldPlayerState>(PlayerState))
    {
        Players.Add(SkaldPlayer);
    }
}

ASkaldPlayerState* ASkaldGameState::GetCurrentPlayer() const
{
    return Players.IsValidIndex(CurrentTurnIndex) ? Players[CurrentTurnIndex] : nullptr;
}

