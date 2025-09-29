#pragma once

#if WITH_AUTOMATION_TESTS
#include "CoreMinimal.h"
#include "UI/SkaldMainHUDWidget.h"
#include "DeploySelectionCachingTest.generated.h"

class UDeployWidget;

/**
 * Test-only subclass to expose protected functionality from USkaldMainHUDWidget
 */
UCLASS()
class SKALD_API UTestSkaldMainHUDWidget : public USkaldMainHUDWidget
{
    GENERATED_BODY()

public:
    using USkaldMainHUDWidget::HandleDeployClicked;

    UDeployWidget* GetActiveDeployWidget() const { return ActiveDeployWidget; }
};

#endif // WITH_AUTOMATION_TESTS

