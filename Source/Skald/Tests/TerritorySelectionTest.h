#pragma once

#include "CoreMinimal.h"
#include "Skald_PlayerController.h"
#include "TerritorySelectionTest.generated.h"

class ATerritory;

/** Player controller capturing server selection calls for tests. */
UCLASS()
class SKALD_API ATerritorySelectionTestPC : public ASkaldPlayerController {
    GENERATED_BODY()

public:
    bool bServerSelectCalled = false;

    virtual void ServerSelectTerritory_Implementation(ATerritory *Territory) override {
        bServerSelectCalled = true;
        Super::ServerSelectTerritory_Implementation(Territory);
    }
};

