#pragma once

#include "CoreMinimal.h"
#include "Skald_PlayerController.h"
#include "UI/SkaldMainHUDWidget.h"
#include "DeployUnitsReplicationTest.generated.h"

/** HUD widget capturing the last deployable units value for tests. */
UCLASS()
class UDeployTestHUDWidget : public USkaldMainHUDWidget {
    GENERATED_BODY()
public:
    int32 LastUnits = -1;
    virtual void UpdateDeployableUnits(int32 UnitsRemaining) override { LastUnits = UnitsRemaining; }
};

/** Player controller with accessible HUD setter for tests. */
UCLASS()
class ADeployTestPlayerController : public ASkaldPlayerController {
    GENERATED_BODY()
public:
    void SetHUD(USkaldMainHUDWidget* InHUD) { MainHUD = InHUD; }
};

