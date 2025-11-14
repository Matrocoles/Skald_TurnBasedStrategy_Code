#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SkaldProjectileFXInterface.generated.h"

class AFighterPawn;

UINTERFACE(Blueprintable)
class SKALD_API USkaldProjectileFXInterface : public UInterface
{
    GENERATED_BODY()
};

class SKALD_API ISkaldProjectileFXInterface
{
    GENERATED_BODY()

public:
    /** Provide spawn context so projectile actors can drive their own visuals. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Skald|FX")
    void InitializeProjectileFX(AFighterPawn* SourceFighter, AFighterPawn* TargetFighter,
                                const FVector& StartLocation, const FVector& TargetLocation);
};
