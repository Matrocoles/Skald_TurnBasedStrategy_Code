#pragma once

#include "CoreMinimal.h"
#include "Engine/ReplicationDriver.h"
#include "Skald_ReplicationDriver.generated.h"

/**
 * Minimal replication driver that defers replication back to the owning net driver.
 * This gives the project an explicit ReplicationDriverClass so listen servers in PIE
 * no longer warn about missing configuration while still using the engine defaults.
 */
UCLASS()
class SKALD_API USkaldReplicationDriver : public UReplicationDriver
{
    GENERATED_BODY()

public:
    USkaldReplicationDriver();

    virtual void InitForNetDriver(UNetDriver* InNetDriver) override;
    virtual void ServerReplicateActors(float DeltaSeconds) override;
    virtual void TearDown() override;

private:
    /** Cached pointer to the owning net driver so we can forward replication work. */
    UPROPERTY()
    TObjectPtr<UNetDriver> CachedNetDriver;
};
