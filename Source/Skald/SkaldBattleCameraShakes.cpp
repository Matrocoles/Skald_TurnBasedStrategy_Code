#include "SkaldBattleCameraShakes.h"

// No pattern headers needed; we use legacy matinee oscillation fields.

USkaldHitCameraShake::USkaldHitCameraShake(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Timing
    OscillationDuration   = 0.20f;
    OscillationBlendInTime  = 0.04f;
    OscillationBlendOutTime = 0.12f;

    // Rotation oscillation (degrees & Hz)
    RotOscillation.Pitch.Amplitude = 0.75f;
    RotOscillation.Pitch.Frequency = 30.0f;

    RotOscillation.Yaw.Amplitude   = 0.60f;
    RotOscillation.Yaw.Frequency   = 26.0f;

    RotOscillation.Roll.Amplitude  = 0.0f;
    RotOscillation.Roll.Frequency  = 0.0f;

    // Location oscillation (units & Hz)
    LocOscillation.X.Amplitude = 0.0f;  LocOscillation.X.Frequency = 0.0f;
    LocOscillation.Y.Amplitude = 0.0f;  LocOscillation.Y.Frequency = 0.0f;
    LocOscillation.Z.Amplitude = 1.5f;  LocOscillation.Z.Frequency = 22.0f;

    // No FOV oscillation
    FOVOscillation.Amplitude = 0.0f;
    FOVOscillation.Frequency = 0.0f;
}

USkaldMissCameraShake::USkaldMissCameraShake(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Timing
    OscillationDuration     = 0.12f;
    OscillationBlendInTime  = 0.03f;
    OscillationBlendOutTime = 0.08f;

    // Rotation oscillation
    RotOscillation.Pitch.Amplitude = 0.35f;
    RotOscillation.Pitch.Frequency = 22.0f;

    RotOscillation.Yaw.Amplitude   = 0.30f;
    RotOscillation.Yaw.Frequency   = 20.0f;

    RotOscillation.Roll.Amplitude  = 0.0f;
    RotOscillation.Roll.Frequency  = 0.0f;

    // Location oscillation
    LocOscillation.X.Amplitude = 0.0f;  LocOscillation.X.Frequency = 0.0f;
    LocOscillation.Y.Amplitude = 0.0f;  LocOscillation.Y.Frequency = 0.0f;
    LocOscillation.Z.Amplitude = 0.75f; LocOscillation.Z.Frequency = 18.0f;

    // No FOV oscillation
    FOVOscillation.Amplitude = 0.0f;
    FOVOscillation.Frequency = 0.0f;
}
