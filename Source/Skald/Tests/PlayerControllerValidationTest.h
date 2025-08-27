#pragma once

#include "CoreMinimal.h"
#include "Skald_PlayerController.h"
#include "UI/SkaldMainHUDWidget.h"
#include "PlayerControllerValidationTest.generated.h"

UCLASS()
class UTestHUDWidget : public USkaldMainHUDWidget
{
    GENERATED_BODY()
public:
    FString LastError;
    virtual void ShowErrorMessage(const FString& Message) override;
};

UCLASS()
class ATestPlayerController : public ASkaldPlayerController
{
    GENERATED_BODY()
public:
    void SetHUD(USkaldMainHUDWidget* InHUD);
};

