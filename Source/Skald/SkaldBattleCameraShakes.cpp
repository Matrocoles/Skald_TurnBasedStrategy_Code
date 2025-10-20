#include "SkaldBattleCameraShakes.h"

#include "Camera/CameraShakePattern.h"
#include "Camera/OscillatorCameraShakePattern.h"

USkaldOscillationCameraShake::USkaldOscillationCameraShake(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Intentionally empty; subclasses will call BuildOscillationPattern with their tuning.
}

void USkaldOscillationCameraShake::BuildOscillationPattern(
    const FRotator& RotAmplitude,
    const FRotator& RotFrequency,
    const FVector&  LocAmplitude,
    const FVector&  LocFrequency,
    float DurationSeconds,
    float BlendInSeconds,
    float BlendOutSeconds)
{
    // Create an oscillator pattern instance and wire it as the root pattern.
    UOscillatorCameraShakePattern* Pattern = NewObject<UOscillatorCameraShakePattern>(this);
    check(Pattern);

    // Rotation oscillation
    Pattern->RotOscillation.Pitch.Amplitude   = RotAmplitude.Pitch;
    Pattern->RotOscillation.Pitch.Frequency   = RotFrequency.Pitch;
    Pattern->RotOscillation.Yaw.Amplitude     = RotAmplitude.Yaw;
    Pattern->RotOscillation.Yaw.Frequency     = RotFrequency.Yaw;
    Pattern->RotOscillation.Roll.Amplitude    = RotAmplitude.Roll;
    Pattern->RotOscillation.Roll.Frequency    = RotFrequency.Roll;

    // Location oscillation
    Pattern->LocOscillation.X.Amplitude       = LocAmplitude.X;
    Pattern->LocOscillation.X.Frequency       = LocFrequency.X;
    Pattern->LocOscillation.Y.Amplitude       = LocAmplitude.Y;
    Pattern->LocOscillation.Y.Frequency       = LocFrequency.Y;
    Pattern->LocOscillation.Z.Amplitude       = LocAmplitude.Z;
    Pattern->LocOscillation.Z.Frequency       = LocFrequency.Z;

    // Timing
    Pattern->Duration                         = DurationSeconds;
    Pattern->BlendInTime                      = BlendInSeconds;
    Pattern->BlendOutTime                     = BlendOutSeconds;

    // Apply
    SetRootShakePattern(Pattern);
}

USkaldHitCameraShake::USkaldHitCameraShake(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Tuned for a crisp, readable “hit” jolt.
    BuildOscillationPattern(
        /*RotAmp*/ FRotator(0.75f, 0.60f, 0.0f),
        /*RotHz */ FRotator(30.0f, 26.0f, 0.0f),
        /*LocAmp*/ FVector(0.0f, 0.0f, 1.5f),
        /*LocHz */ FVector(0.0f, 0.0f, 22.0f),
        /*Dur   */ 0.20f,
        /*In    */ 0.04f,
        /*Out   */ 0.12f
    );
}

USkaldMissCameraShake::USkaldMissCameraShake(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Subtler, so it doesn’t feel punishing on frequent misses.
    BuildOscillationPattern(
        /*RotAmp*/ FRotator(0.35f, 0.30f, 0.0f),
        /*RotHz */ FRotator(22.0f, 20.0f, 0.0f),
        /*LocAmp*/ FVector(0.0f, 0.0f, 0.75f),
        /*LocHz */ FVector(0.0f, 0.0f, 18.0f),
        /*Dur   */ 0.12f,
        /*In    */ 0.03f,
        /*Out   */ 0.08f
    );
}
