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
    if (!PlayerArray.IsValidIndex(CurrentTurnIndex))
    {
        return nullptr;
    }

    return Cast<ASkaldPlayerState>(PlayerArray[CurrentTurnIndex]);
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
    const int32 PlayerCount = PlayerArray.Num();
    if (PlayerCount == 0)
    {
        CurrentTurnIndex = 0;
        return;
    }

    if (CurrentTurnIndex < 0 || CurrentTurnIndex >= PlayerCount)
    {
        CurrentTurnIndex = FMath::Clamp(CurrentTurnIndex, 0, FMath::Max(0, PlayerCount - 1));
    }
}

void ASkaldGameState::SortAndDedupPlayers()
{
    // Remove any null entries that can occur temporarily during level travel
    // while PlayerStates are re-initialising. Attempting to dereference them
    // during the sort would otherwise cause access violations when returning
    // to the world map after a battle.
    Players.RemoveAll([](const TObjectPtr<ASkaldPlayerState>& Player)
    {
        return Player.Get() == nullptr;
    });

    // Stable order by PlayerId for deterministic turns/HUD lists. Null entries
    // have been filtered out above so it is now safe to dereference.
    Players.Sort([](const TObjectPtr<ASkaldPlayerState>& A, const TObjectPtr<ASkaldPlayerState>& B)
    {
        ASkaldPlayerState* const PlayerA = A.Get();
        ASkaldPlayerState* const PlayerB = B.Get();
        if (PlayerA == PlayerB)
        {
            return false;
        }
        if (!PlayerA)
        {
            return false;
        }
        if (!PlayerB)
        {
            return true;
        }
        return PlayerA->GetPlayerId() < PlayerB->GetPlayerId();
    });

    // Dedup in case the engine calls AddPlayerState twice for the same actor
    // (defensive)
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

