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
        return CachedNetDriver->ServerReplicateActors(DeltaSeconds);
    }

    return Super::ServerReplicateActors(DeltaSeconds);
}

void USkaldReplicationDriver::TearDown()
{
    CachedNetDriver = nullptr;
}
