#include "Skald_PlayerState.h"
#include "Net/UnrealNetwork.h"

ASkaldPlayerState::ASkaldPlayerState()
    : bIsAI(false)
    , ArmyPool(0)
    , InitiativeRoll(0)
    , DisplayName(TEXT("Player"))
    , Faction(ESkaldFaction::None)
{
}

void ASkaldPlayerState::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ASkaldPlayerState, DisplayName);
    DOREPLIFETIME(ASkaldPlayerState, Faction);
    DOREPLIFETIME(ASkaldPlayerState, ArmyPool);
}

