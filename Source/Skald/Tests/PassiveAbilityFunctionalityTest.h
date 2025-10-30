#pragma once

#include "CoreMinimal.h"
#include "Containers/Set.h"
#include "GridOverlayComponent.h"
#include "PassiveAbilityFunctionalityTest.generated.h"

/**
 * Test double allowing the difficult terrain map to be controlled directly.
 */
UCLASS()
class SKALD_API UTestGridOverlayComponent : public UGridOverlayComponent
{
    GENERATED_BODY()

public:
    void SetDifficultCell(const FIntPoint& Cell, bool bIsDifficult);

    virtual bool IsDifficultTerrain(const FIntPoint& GridCoord) const override;

private:
    UPROPERTY()
    TSet<FIntPoint> DifficultCells;
};

