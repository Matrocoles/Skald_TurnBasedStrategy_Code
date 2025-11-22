#include "Skald_GameState.h"
#include "Skald_PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "HAL/PlatformTime.h"
#include "Skald_PlayerController.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "SkaldLogging.h"
#include "Skald_GameInstance.h"

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
    DOREPLIFETIME(ASkaldGameState, PendingBattleReadyState);
    DOREPLIFETIME(ASkaldGameState, ActiveBattlePayload);
    DOREPLIFETIME(ASkaldGameState, BattleEntries);
    DOREPLIFETIME(ASkaldGameState, CurrentBattleRound);
    DOREPLIFETIME(ASkaldGameState, BattleInitiativeWinner);
    DOREPLIFETIME(ASkaldGameState, RemainingAttackerActivations);
    DOREPLIFETIME(ASkaldGameState, RemainingDefenderActivations);
    DOREPLIFETIME(ASkaldGameState, bBattleAttackerTurn);
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

void ASkaldGameState::SetPendingBattleReady(const FSkaldBattleReadyState& NewState)
{
    if (!HasAuthority())
    {
        UE_LOG(LogSkaldBattle, Warning,
               TEXT("SetPendingBattleReady called without authority."));
        return;
    }

    PendingBattleReadyState = NewState;

    if (UWorld* World = GetWorld())
    {
        PendingBattleReadyState.LastUpdatedTimeSeconds = World->GetTimeSeconds();
    }
    else
    {
        PendingBattleReadyState.LastUpdatedTimeSeconds = FPlatformTime::Seconds();
    }

    OnRep_PendingBattleReady();
}

void ASkaldGameState::SetBattleRoundState(int32 RoundNumber, ESkaldFaction InitiativeWinner, int32 AttackerActivations,
                                          int32 DefenderActivations, bool bIsAttackerTurn)
{
    if (!HasAuthority())
    {
        return;
    }

    bool bChanged = false;

    if (CurrentBattleRound != RoundNumber)
    {
        CurrentBattleRound = RoundNumber;
        bChanged = true;
    }

    if (BattleInitiativeWinner != InitiativeWinner)
    {
        BattleInitiativeWinner = InitiativeWinner;
        bChanged = true;
    }

    if (RemainingAttackerActivations != AttackerActivations)
    {
        RemainingAttackerActivations = AttackerActivations;
        bChanged = true;
    }

    if (RemainingDefenderActivations != DefenderActivations)
    {
        RemainingDefenderActivations = DefenderActivations;
        bChanged = true;
    }

    if (bBattleAttackerTurn != bIsAttackerTurn)
    {
        bBattleAttackerTurn = bIsAttackerTurn;
        bChanged = true;
    }

    if (bChanged)
    {
        OnRep_BattleRoundState();
    }
}

bool ASkaldGameState::AreAllRequiredPartiesReady() const
{
    const bool bAttackerReady =
        PendingBattleReadyState.AttackerPlayerID == INDEX_NONE ||
        PendingBattleReadyState.bAttackerReady;

    const bool bDefenderReady =
        PendingBattleReadyState.DefenderPlayerID == INDEX_NONE ||
        PendingBattleReadyState.bDefenderReady;

    return bAttackerReady && bDefenderReady;
}

int32 ASkaldGameState::GetRemainingActivations(bool bForAttackers) const
{
    return bForAttackers ? RemainingAttackerActivations : RemainingDefenderActivations;
}

void ASkaldGameState::OnRep_BattleSummary()
{
    OnBattleSummaryUpdated.Broadcast();
}

void ASkaldGameState::OnRep_BattlePayload()
{
    if (USkaldGameInstance* GI = GetGameInstance<USkaldGameInstance>())
    {
        GI->PendingBattle = ActiveBattlePayload;
    }
}

void ASkaldGameState::OnRep_BattleEntries()
{
    UE_LOG(LogSkaldBattle, Log,
           TEXT("GameState BattleEntries replicated (%d participants)"),
           BattleEntries.Num());
    OnBattleEntriesUpdated.Broadcast();
}

void ASkaldGameState::OnRep_PendingBattleReady()
{
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("GameState pending battle ready state updated: Attacker=%d Ready=%s AI=%s, Defender=%d Ready=%s AI=%s (t=%.3f)"),
           PendingBattleReadyState.AttackerPlayerID,
           PendingBattleReadyState.bAttackerReady ? TEXT("true") : TEXT("false"),
           PendingBattleReadyState.bAttackerIsAI ? TEXT("true") : TEXT("false"),
           PendingBattleReadyState.DefenderPlayerID,
           PendingBattleReadyState.bDefenderReady ? TEXT("true") : TEXT("false"),
           PendingBattleReadyState.bDefenderIsAI ? TEXT("true") : TEXT("false"),
           PendingBattleReadyState.LastUpdatedTimeSeconds);
}

void ASkaldGameState::OnRep_BattleRoundState()
{
    OnBattleRoundUpdated.Broadcast(CurrentBattleRound, BattleInitiativeWinner, RemainingAttackerActivations,
                                   RemainingDefenderActivations, bBattleAttackerTurn);
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

void ASkaldGameState::SetActiveBattlePayload(const FS_BattlePayload& Payload)
{
  if (!HasAuthority())
  {
    return;
  }

  ActiveBattlePayload = Payload;
  OnRep_BattlePayload();
  ForceNetUpdate();
}

void ASkaldGameState::UpsertBattleEntry(const FBattlePlayerEntry& Entry)
{
    if (!HasAuthority())
    {
        return;
    }

    if (Entry.PlayerId <= 0)
    {
        return;
    }

    bool bUpdated = false;
    for (FBattlePlayerEntry& Existing : BattleEntries)
    {
        if (Existing.PlayerId == Entry.PlayerId)
        {
            Existing = Entry;
            bUpdated = true;
            break;
        }
    }

  if (!bUpdated)
  {
    BattleEntries.Add(Entry);
  }

  OnRep_BattleEntries();
  ForceNetUpdate();
}

bool ASkaldGameState::GetBattleEntryForPlayer(int32 PlayerId, FBattlePlayerEntry& OutEntry) const
{
    const FBattlePlayerEntry* Found = BattleEntries.FindByPredicate([PlayerId](const FBattlePlayerEntry& Candidate)
    {
        return Candidate.PlayerId == PlayerId;
    });

    if (Found)
    {
        OutEntry = *Found;
        return true;
    }

    return false;
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

    if (BattlePhase == EBattlePhase::FighterSelection)
    {
        UE_LOG(LogSkaldBattle, Log, TEXT("Broadcasting fighter selection phase to controllers"));
    }

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

void ASkaldGameState::RequestTransientSlowdown(float TargetDilation, float DurationSeconds)
{
    if (TargetDilation <= 0.f || DurationSeconds <= 0.f)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World || World->IsPaused())
    {
        return;
    }

    if (AWorldSettings* WorldSettings = World->GetWorldSettings())
    {
        const float ClampedTarget = FMath::Clamp(TargetDilation, 0.01f, 1.f);
        const float CurrentDilation = WorldSettings->GetEffectiveTimeDilation();

        if (!bTimeDilationRequestActive)
        {
            OriginalTimeDilation = CurrentDilation > 0.f ? CurrentDilation : 1.f;
            ActiveTimeDilation = OriginalTimeDilation;
            bTimeDilationRequestActive = true;
        }

        const float DesiredDilation = FMath::Min(ActiveTimeDilation, ClampedTarget);
        ActiveTimeDilation = DesiredDilation;

        if (CurrentDilation - DesiredDilation > KINDA_SMALL_NUMBER)
        {
            WorldSettings->SetTimeDilation(DesiredDilation);
        }

        FTimerManager& TimerManager = World->GetTimerManager();
        TimerManager.ClearTimer(TimeDilationResetHandle);
        TimerManager.SetTimer(TimeDilationResetHandle, this, &ASkaldGameState::HandleTimeDilationReset, DurationSeconds, false);
    }
}

void ASkaldGameState::HandleTimeDilationReset()
{
    if (!bTimeDilationRequestActive)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        bTimeDilationRequestActive = false;
        ActiveTimeDilation = 1.f;
        OriginalTimeDilation = 1.f;
        return;
    }

    AWorldSettings* WorldSettings = World->GetWorldSettings();
    if (WorldSettings)
    {
        const float CurrentDilation = WorldSettings->GetEffectiveTimeDilation();
        if (FMath::IsNearlyEqual(CurrentDilation, ActiveTimeDilation, KINDA_SMALL_NUMBER))
        {
            const float RestoreValue = OriginalTimeDilation > 0.f ? OriginalTimeDilation : 1.f;
            WorldSettings->SetTimeDilation(RestoreValue);
        }
    }

    World->GetTimerManager().ClearTimer(TimeDilationResetHandle);
    bTimeDilationRequestActive = false;
    ActiveTimeDilation = 1.f;
    OriginalTimeDilation = 1.f;
}

void ASkaldGameState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    HandleTimeDilationReset();
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TimeDilationResetHandle);
    }

    Super::EndPlay(EndPlayReason);
}

