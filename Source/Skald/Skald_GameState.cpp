#include "Skald_GameState.h"
#include "Skald_PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Skald_PlayerController.h"
#include "Engine/World.h"
#include "SkaldLogging.h"

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
    DOREPLIFETIME(ASkaldGameState, FighterRoster); // NEW
    DOREPLIFETIME(ASkaldGameState, LastBattleWinner);
    DOREPLIFETIME(ASkaldGameState, LastAttackerCasualties);
    DOREPLIFETIME(ASkaldGameState, LastDefenderCasualties);
    DOREPLIFETIME(ASkaldGameState, BattlePhase);
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

void ASkaldGameState::OnRep_FighterRoster()
{
    OnFighterRosterUpdated.Broadcast();
}

void ASkaldGameState::NotifyBattleSummaryChanged()
{
    OnRep_BattleSummary();
}

void ASkaldGameState::OnRep_BattleSummary()
{
    OnBattleSummaryUpdated.Broadcast();
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

void ASkaldGameState::ServerSetFighterRoster_Implementation(const TArray<FFighterDefinition>& InRoster)
{
    FighterRoster = InRoster;
    // Fire local notify so server-side UI (if any) also refreshes immediately
    OnRep_FighterRoster();
}

void ASkaldGameState::SetBattlePhase(EBattlePhase NewPhase)
{
    if (HasAuthority() && BattlePhase != NewPhase)
    {
        BattlePhase = NewPhase;
        OnRep_BattlePhase();
    }
}

void ASkaldGameState::OnRep_BattlePhase()
{
    UE_LOG(LogSkaldBattle, Log, TEXT("GameState BattlePhase -> %d"), static_cast<int32>(BattlePhase));

    if (UWorld* World = GetWorld())
    {
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
        {
            if (ASkaldPlayerController* PC = Cast<ASkaldPlayerController>(It->Get()))
            {
                PC->HandleBattlePhaseChanged();
            }
        }
    }
}

