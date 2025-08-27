#include "Skald_GameState.h"
#include "Skald_PlayerState.h"
#include "Net/UnrealNetwork.h"

ASkaldGameState::ASkaldGameState()
    : CurrentTurnIndex(0)
{
}

void ASkaldGameState::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ASkaldGameState, Players);
    DOREPLIFETIME(ASkaldGameState, CurrentTurnIndex);
}

void ASkaldGameState::AddPlayerState(APlayerState* PlayerState)
{
    Super::AddPlayerState(PlayerState);

    if (ASkaldPlayerState* SkaldPlayer = Cast<ASkaldPlayerState>(PlayerState))
    {
        Players.Add(SkaldPlayer);
        OnPlayersUpdated.Broadcast();
    }
}

ASkaldPlayerState* ASkaldGameState::GetCurrentPlayer() const
{
    return Players.IsValidIndex(CurrentTurnIndex) ? Players[CurrentTurnIndex] : nullptr;
}

