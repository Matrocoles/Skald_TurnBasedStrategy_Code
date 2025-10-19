#include "SkaldBattleCameraShakes.h"

#include "Camera/CameraShakePattern.h"
#include "Camera/CameraShakePatterns/OscillatorCameraShakePattern.h"

USkaldHitCameraShake::USkaldHitCameraShake() {
  OscillationPattern =
      CreateDefaultSubobject<UOscillatorCameraShakePattern>(TEXT("HitOscillation"));
  OscillationPattern->Duration = 0.2f;
  OscillationPattern->BlendInTime = 0.04f;
  OscillationPattern->BlendOutTime = 0.12f;

  OscillationPattern->RotOscillation.Pitch.Amplitude = 0.75f;
  OscillationPattern->RotOscillation.Pitch.Frequency = 30.f;
  OscillationPattern->RotOscillation.Yaw.Amplitude = 0.6f;
  OscillationPattern->RotOscillation.Yaw.Frequency = 26.f;

  OscillationPattern->LocOscillation.Z.Amplitude = 1.5f;
  OscillationPattern->LocOscillation.Z.Frequency = 22.f;

  SetRootShakePattern(OscillationPattern);
}

USkaldMissCameraShake::USkaldMissCameraShake() {
  OscillationPattern =
      CreateDefaultSubobject<UOscillatorCameraShakePattern>(TEXT("MissOscillation"));
  OscillationPattern->Duration = 0.12f;
  OscillationPattern->BlendInTime = 0.03f;
  OscillationPattern->BlendOutTime = 0.08f;

  OscillationPattern->RotOscillation.Pitch.Amplitude = 0.35f;
  OscillationPattern->RotOscillation.Pitch.Frequency = 22.f;
  OscillationPattern->RotOscillation.Yaw.Amplitude = 0.3f;
  OscillationPattern->RotOscillation.Yaw.Frequency = 20.f;

  OscillationPattern->LocOscillation.Z.Amplitude = 0.75f;
  OscillationPattern->LocOscillation.Z.Frequency = 18.f;

  SetRootShakePattern(OscillationPattern);
}

