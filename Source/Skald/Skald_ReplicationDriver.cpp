#include "Skald_ReplicationDriver.h"

#include "Engine/NetDriver.h"

USkaldReplicationDriver::USkaldReplicationDriver()
    : CachedNetDriver(nullptr)
{
}

void USkaldReplicationDriver::InitForNetDriver(UNetDriver* InNetDriver)
{
    CachedNetDriver = InNetDriver;
}

auto USkaldReplicationDriver::ServerReplicateActors(float DeltaSeconds)
    -> FServerReplicateActorsResult
{
    if (CachedNetDriver)
    {
        return CachedNetDriver->ServerReplicateActors_ProcessLoadedLevels(DeltaSeconds);
    }

    return {};
}

void USkaldReplicationDriver::TearDown()
{
    CachedNetDriver = nullptr;
}
