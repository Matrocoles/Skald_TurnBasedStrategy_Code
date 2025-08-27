#include "Skald_PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Skald_GameState.h"

ASkaldPlayerState::ASkaldPlayerState()
    : bIsAI(false)
    , ArmyPool(0)
    , InitiativeRoll(0)
    , DisplayName(TEXT("Player"))
    , Faction(ESkaldFaction::None)
    , IsEliminated(false)
{
}

void ASkaldPlayerState::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ASkaldPlayerState, DisplayName);
    DOREPLIFETIME(ASkaldPlayerState, Faction);
    DOREPLIFETIME(ASkaldPlayerState, ArmyPool);
    DOREPLIFETIME(ASkaldPlayerState, bIsAI);
    DOREPLIFETIME(ASkaldPlayerState, InitiativeRoll);
    DOREPLIFETIME(ASkaldPlayerState, IsEliminated);
}

void ASkaldPlayerState::OnRep_IsEliminated()
{
    if (ASkaldGameState* GS = GetWorld() ? GetWorld()->GetGameState<ASkaldGameState>() : nullptr)
    {
        GS->OnPlayersUpdated.Broadcast();
    }
}

