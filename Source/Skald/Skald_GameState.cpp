#include "Skald_GameState.h"
#include "Skald_PlayerState.h"

ASkaldGameState::ASkaldGameState()
    : CurrentTurnIndex(0)
{
}

void ASkaldGameState::AddPlayerState(ASkaldPlayerState* PlayerState)
{
    if (PlayerState)
    {
        Players.Add(PlayerState);
    }
}

ASkaldPlayerState* ASkaldGameState::GetCurrentPlayer() const
{
    return Players.IsValidIndex(CurrentTurnIndex) ? Players[CurrentTurnIndex] : nullptr;
}

