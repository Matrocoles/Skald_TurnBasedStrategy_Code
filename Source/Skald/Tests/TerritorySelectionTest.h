#pragma once

#if WITH_AUTOMATION_TESTS
#include "CoreMinimal.h"
#include "Skald_PlayerController.h"
#include "TerritorySelectionTest.generated.h"

/** Player controller capturing server selection calls for tests. */
UCLASS()
class ATerritorySelectionTestPC : public ASkaldPlayerController {
    GENERATED_BODY()
public:
    bool bServerSelectCalled = false;
    virtual void ServerSelectTerritory_Implementation(int32 TerritoryID) override {
        bServerSelectCalled = true;
        Super::ServerSelectTerritory_Implementation(TerritoryID);
    }
};

#endif // WITH_AUTOMATION_TESTS

