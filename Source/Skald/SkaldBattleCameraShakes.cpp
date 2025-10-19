#include "SkaldBattleCameraShakes.h"

#include "Camera/MatineeCameraShakePattern.h"

USkaldHitCameraShake::USkaldHitCameraShake() {
  if (UMatineeCameraShakePattern* Pattern = GetCameraShakePattern()) {
    Pattern->OscillationDuration = 0.2f;
    Pattern->OscillationBlendInTime = 0.04f;
    Pattern->OscillationBlendOutTime = 0.12f;

    Pattern->RotOscillation.Pitch.Amplitude = 0.75f;
    Pattern->RotOscillation.Pitch.Frequency = 30.f;
    Pattern->RotOscillation.Yaw.Amplitude = 0.6f;
    Pattern->RotOscillation.Yaw.Frequency = 26.f;

    Pattern->LocOscillation.Z.Amplitude = 1.5f;
    Pattern->LocOscillation.Z.Frequency = 22.f;
  }
}

USkaldMissCameraShake::USkaldMissCameraShake() {
  if (UMatineeCameraShakePattern* Pattern = GetCameraShakePattern()) {
    Pattern->OscillationDuration = 0.12f;
    Pattern->OscillationBlendInTime = 0.03f;
    Pattern->OscillationBlendOutTime = 0.08f;

    Pattern->RotOscillation.Pitch.Amplitude = 0.35f;
    Pattern->RotOscillation.Pitch.Frequency = 22.f;
    Pattern->RotOscillation.Yaw.Amplitude = 0.3f;
    Pattern->RotOscillation.Yaw.Frequency = 20.f;

    Pattern->LocOscillation.Z.Amplitude = 0.75f;
    Pattern->LocOscillation.Z.Frequency = 18.f;
  }
}

