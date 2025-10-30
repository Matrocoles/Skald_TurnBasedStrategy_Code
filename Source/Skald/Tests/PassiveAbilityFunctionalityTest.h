#pragma once

#include "CoreMinimal.h"
#include "GridOverlayComponent.h"

#include "PassiveAbilityFunctionalityTest.generated.h"

#if WITH_AUTOMATION_TESTS

UCLASS()
class UTestGridOverlayComponent final : public UGridOverlayComponent
{
    GENERATED_BODY()

public:
    void SetDifficultCell(const FIntPoint& Cell, bool bIsDifficult);

    virtual bool IsDifficultTerrain(const FIntPoint& GridCoord) const override;

private:
    UPROPERTY()
    TSet<FIntPoint> DifficultCells;
};

#endif // WITH_AUTOMATION_TESTS
