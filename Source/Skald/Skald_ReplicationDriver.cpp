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

void USkaldReplicationDriver::TearDown()
{
    CachedNetDriver = nullptr;
}
