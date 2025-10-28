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
        // Call the underlying net driver's internal replication routine rather than
        // re-entering UNetDriver::ServerReplicateActors, which would immediately
        // delegate back to this replication driver and recurse indefinitely.
        return CachedNetDriver->ServerReplicateActors_ProcessLoadedLevels(DeltaSeconds);
    }

    return Super::ServerReplicateActors(DeltaSeconds);
}

void USkaldReplicationDriver::TearDown()
{
    CachedNetDriver = nullptr;
}
