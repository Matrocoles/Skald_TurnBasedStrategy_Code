#include "LobbyGameState.h"

#include "Net/UnrealNetwork.h"

ALobbyGameState::ALobbyGameState()
{
    LobbySlots.SetNum(4);
    TotalSlots = 2;
    ReservedAISlots = 0;
}

void ALobbyGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ALobbyGameState, LobbySlots);
    DOREPLIFETIME(ALobbyGameState, TotalSlots);
    DOREPLIFETIME(ALobbyGameState, ReservedAISlots);
    DOREPLIFETIME(ALobbyGameState, bSlotConfigurationLocked);
}

bool ALobbyGameState::AreAllSlotsReady() const
{
    const int32 ActiveSlots = FMath::Clamp(TotalSlots, 0, LobbySlots.Num());
    for (int32 Index = 0; Index < ActiveSlots; ++Index)
    {
        const FLobbyPlayerSlot& Slot = LobbySlots[Index];
        if (!Slot.bIsActive)
        {
            continue;
        }

        if (!Slot.bIsReady)
        {
            return false;
        }
    }

    return true;
}

int32 ALobbyGameState::FindSlotIndexForPlayer(int32 PlayerId) const
{
    if (PlayerId == INDEX_NONE)
    {
        return INDEX_NONE;
    }

    for (int32 Index = 0; Index < LobbySlots.Num(); ++Index)
    {
        const FLobbyPlayerSlot& Slot = LobbySlots[Index];
        if (Slot.PlayerId == PlayerId)
        {
            return Index;
        }
    }

    return INDEX_NONE;
}

const FLobbyPlayerSlot* ALobbyGameState::GetSlot(int32 Index) const
{
    if (!LobbySlots.IsValidIndex(Index))
    {
        return nullptr;
    }

    return &LobbySlots[Index];
}

void ALobbyGameState::OnRep_LobbySlots()
{
    BroadcastSlotsUpdated();
}

void ALobbyGameState::OnRep_TotalSlots()
{
    BroadcastSlotsUpdated();
}

void ALobbyGameState::OnRep_AISlots()
{
    BroadcastSlotsUpdated();
}

void ALobbyGameState::OnRep_SlotConfigurationLocked()
{
    BroadcastSlotsUpdated();
}

void ALobbyGameState::BroadcastSlotsUpdated()
{
    OnLobbySlotsUpdated.Broadcast();
}

