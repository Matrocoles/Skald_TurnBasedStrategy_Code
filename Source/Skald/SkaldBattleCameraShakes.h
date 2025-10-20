#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "SkaldBattleCameraShakes.generated.h"

/**
 * Base class for Skald camera shakes using the UE5 pattern system.
 * We DO NOT override Start/Update/Stop/IsFinished.
 * Instead we construct and assign a UOscillatorCameraShakePattern at runtime.
 */
UCLASS(Abstract)
class SKALD_API USkaldOscillationCameraShake : public UCameraShakeBase
{
    GENERATED_BODY()

public:
    explicit USkaldOscillationCameraShake(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
    /** Helper to create and wire up an oscillator pattern with the given params. */
    void BuildOscillationPattern(
        const FRotator& RotAmplitude,     // degrees
        const FRotator& RotFrequency,     // Hz
        const FVector&  LocAmplitude,     // units
        const FVector&  LocFrequency,     // Hz
        float DurationSeconds,
        float BlendInSeconds,
        float BlendOutSeconds
    );
};

/** Stronger hit feedback. */
UCLASS()
class SKALD_API USkaldHitCameraShake : public USkaldOscillationCameraShake
{
    GENERATED_BODY()

public:
    explicit USkaldHitCameraShake(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

/** Subtle miss/near-miss feedback. */
UCLASS()
class SKALD_API USkaldMissCameraShake : public USkaldOscillationCameraShake
{
    GENERATED_BODY()

public:
    explicit USkaldMissCameraShake(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
