#include "Skald_PlayerState.h"

ASkaldPlayerState::ASkaldPlayerState()
    : bIsAI(false)
    , ArmyPool(0)
    , InitiativeRoll(0)
    , DisplayName(TEXT("Player"))
    , Faction(ESkaldFaction::None)
{
}

