#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShake.h"   // UMatineeCameraShake
#include "SkaldBattleCameraShakes.generated.h"

/**
 * Stronger hit feedback shake (legacy matinee shake for broad UE5 compatibility).
 */
UCLASS()
class SKALD_API USkaldHitCameraShake : public UMatineeCameraShake
{
    GENERATED_BODY()

public:
    explicit USkaldHitCameraShake(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

/**
 * Subtler miss/near-miss feedback shake.
 */
UCLASS()
class SKALD_API USkaldMissCameraShake : public UMatineeCameraShake
{
    GENERATED_BODY()

public:
    explicit USkaldMissCameraShake(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
