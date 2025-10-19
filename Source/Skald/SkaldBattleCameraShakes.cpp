#include "SkaldBattleCameraShakes.h"

USkaldHitCameraShake::USkaldHitCameraShake() {
  OscillationDuration = 0.2f;
  OscillationBlendInTime = 0.04f;
  OscillationBlendOutTime = 0.12f;

  RotOscillation.Pitch.Amplitude = 0.75f;
  RotOscillation.Pitch.Frequency = 30.f;
  RotOscillation.Yaw.Amplitude = 0.6f;
  RotOscillation.Yaw.Frequency = 26.f;

  LocOscillation.Z.Amplitude = 1.5f;
  LocOscillation.Z.Frequency = 22.f;
}

USkaldMissCameraShake::USkaldMissCameraShake() {
  OscillationDuration = 0.12f;
  OscillationBlendInTime = 0.03f;
  OscillationBlendOutTime = 0.08f;

  RotOscillation.Pitch.Amplitude = 0.35f;
  RotOscillation.Pitch.Frequency = 22.f;
  RotOscillation.Yaw.Amplitude = 0.3f;
  RotOscillation.Yaw.Frequency = 20.f;

  LocOscillation.Z.Amplitude = 0.75f;
  LocOscillation.Z.Frequency = 18.f;
}

