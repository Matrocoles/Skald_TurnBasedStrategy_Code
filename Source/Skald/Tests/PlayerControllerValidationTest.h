#pragma once

#include "CoreMinimal.h"
#include "Skald_PlayerController.h"
#include "UI/SkaldMainHUDWidget.h"
#include "PlayerControllerValidationTest.generated.h"

UCLASS()
class SKALD_API UTestHUDWidget : public USkaldMainHUDWidget
{
    GENERATED_BODY()

public:
    FString LastError;

    virtual void ShowErrorMessage(const FString& Message) override
    {
        LastError = Message;
    }
};

UCLASS()
class SKALD_API ATestPlayerController : public ASkaldPlayerController
{
    GENERATED_BODY()

public:
    void SetHUD(USkaldMainHUDWidget* InHUD)
    {
        MainHUD = InHUD;
    }

    bool TestValidateAttack(int32 FromID, int32 ToID, int32 ArmySent, bool bUseSiege, FString* OutError)
    {
        return ValidateAttack(FromID, ToID, ArmySent, bUseSiege, OutError);
    }
};

