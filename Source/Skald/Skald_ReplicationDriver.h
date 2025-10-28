#pragma once

#include "CoreMinimal.h"
#include "Engine/ReplicationDriver.h"
#include "Skald_ReplicationDriver.generated.h"

/**
 * Minimal replication driver that allows the project to opt into an explicit
 * ReplicationDriverClass while relying on the engine's default replication
 * behaviour. We still cache the associated UNetDriver so future
 * customisations can reference it if necessary.
 */
UCLASS()
class SKALD_API USkaldReplicationDriver : public UReplicationDriver
{
    GENERATED_BODY()

public:
    USkaldReplicationDriver();

    virtual void InitForNetDriver(UNetDriver* InNetDriver) override;
    virtual void TearDown() override;

private:
    /** Cached pointer to the owning net driver so we can forward replication work. */
    UPROPERTY()
    TObjectPtr<UNetDriver> CachedNetDriver;
};
