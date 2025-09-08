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
        // Avoid duplicates
        if (!Players.Contains(SkaldPlayer))
        {
            Players.Add(SkaldPlayer);
            SortAndDedupPlayers();
            ClampTurnIndex();
            OnPlayersUpdated.Broadcast();
        }
    }
}

void ASkaldGameState::RemovePlayerState(APlayerState* PlayerState)
{
    Super::RemovePlayerState(PlayerState);

    if (ASkaldPlayerState* SkaldPlayer = Cast<ASkaldPlayerState>(PlayerState))
    {
        const int32 RemovedIndex = Players.IndexOfByKey(SkaldPlayer);
        if (RemovedIndex != INDEX_NONE)
        {
            Players.RemoveAt(RemovedIndex);
            ClampTurnIndex();
            OnPlayersUpdated.Broadcast();
            // Also notify turn change if index moved
            OnTurnIndexChanged.Broadcast(CurrentTurnIndex);
        }
    }
}

ASkaldPlayerState* ASkaldGameState::GetCurrentPlayer() const
{
    return Players.IsValidIndex(CurrentTurnIndex) ? Players[CurrentTurnIndex] : nullptr;
}

ASkaldPlayerState* ASkaldGameState::GetPlayerById(int32 PlayerID) const
{
    for (ASkaldPlayerState* PS : Players)
    {
        if (PS && PS->GetPlayerId() == PlayerID)
        {
            return PS;
        }
    }
    return nullptr;
}

void ASkaldGameState::OnRep_Players()
{
    SortAndDedupPlayers();
    ClampTurnIndex();
    OnPlayersUpdated.Broadcast();
}

void ASkaldGameState::OnRep_CurrentTurnIndex()
{
    ClampTurnIndex();
    OnTurnIndexChanged.Broadcast(CurrentTurnIndex);
}

void ASkaldGameState::ClampTurnIndex()
{
    if (Players.Num() == 0)
    {
        CurrentTurnIndex = 0;
        return;
    }
    if (CurrentTurnIndex < 0 || CurrentTurnIndex >= Players.Num())
    {
        CurrentTurnIndex = FMath::Clamp(CurrentTurnIndex, 0, FMath::Max(0, Players.Num() - 1));
    }
}

void ASkaldGameState::SortAndDedupPlayers()
{
    // Stable order by PlayerId for deterministic turns/HUD lists
    Players.Sort([](const ASkaldPlayerState& A, const ASkaldPlayerState& B)
    {
        return A.GetPlayerId() < B.GetPlayerId();
    });
    // Dedup in case engine calls AddPlayerState twice for same actor (defensive)
    for (int32 i = Players.Num() - 1; i > 0; --i)
    {
        if (Players[i] == Players[i - 1])
        {
            Players.RemoveAt(i);
        }
    }
}

