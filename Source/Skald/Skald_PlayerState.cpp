#include "Skald_PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Skald_GameState.h"
#include "Engine/World.h"

ASkaldPlayerState::ASkaldPlayerState()
    : bIsAI(false)
    , ArmyPool(0)
    , InitiativeRoll(0)
    , Resources(0)
    , PlayerDisplayName(TEXT("Player"))
    , Faction(ESkaldFaction::None)
    , bHasLockedIn(false)
    , IsEliminated(false)
{
}

void ASkaldPlayerState::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ASkaldPlayerState, PlayerDisplayName);
    DOREPLIFETIME(ASkaldPlayerState, Faction);
    DOREPLIFETIME(ASkaldPlayerState, ArmyPool);
    DOREPLIFETIME(ASkaldPlayerState, bIsAI);
    DOREPLIFETIME(ASkaldPlayerState, InitiativeRoll);
    DOREPLIFETIME(ASkaldPlayerState, Resources);
    DOREPLIFETIME(ASkaldPlayerState, bHasLockedIn);
    DOREPLIFETIME(ASkaldPlayerState, IsEliminated);
}

void ASkaldPlayerState::OnRep_HasLockedIn()
{
    if (UWorld* World = GetWorld())
    {
        if (ASkaldGameState* GS = World->GetGameState<ASkaldGameState>())
        {
            GS->OnPlayersUpdated.Broadcast();
        }
    }
}

void ASkaldPlayerState::OnRep_IsEliminated()
{
    if (UWorld* World = GetWorld())
    {
        if (ASkaldGameState* GS = World->GetGameState<ASkaldGameState>())
        {
            GS->OnPlayersUpdated.Broadcast();
        }
    }
}

