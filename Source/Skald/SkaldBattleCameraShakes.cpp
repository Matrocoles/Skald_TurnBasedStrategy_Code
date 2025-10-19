#include "SkaldBattleCameraShakes.h"

#include "Camera/CameraTypes.h"
#include "Math/UnrealMathUtility.h"

namespace {
float ComputeBlendFactor(float Time, float Duration, float BlendInTime,
                         float BlendOutTime) {
  if (Duration <= 0.f) {
    return 0.f;
  }

  const float RemainingTime = Duration - Time;
  float Blend = 1.f;

  if (BlendInTime > 0.f) {
    Blend = FMath::Min(Blend, FMath::Clamp(Time / BlendInTime, 0.f, 1.f));
  }

  if (BlendOutTime > 0.f) {
    Blend = FMath::Min(Blend,
                       FMath::Clamp(RemainingTime / BlendOutTime, 0.f, 1.f));
  }

  return Blend;
}
} // namespace

USkaldOscillationCameraShake::USkaldOscillationCameraShake()
    : Duration(0.2f),
      BlendInTime(0.05f),
      BlendOutTime(0.05f),
      RotationAmplitude(ForceInitToZero),
      RotationFrequency(ForceInitToZero),
      LocationAmplitude(ForceInitToZero),
      LocationFrequency(ForceInitToZero),
      ElapsedTime(0.f),
      bIsActive(false) {}

void USkaldOscillationCameraShake::ConfigureShake(
    float InDuration, float InBlendInTime, float InBlendOutTime,
    const FRotator &InRotationAmplitude, const FRotator &InRotationFrequency,
    const FVector &InLocationAmplitude,
    const FVector &InLocationFrequency) {
  Duration = InDuration;
  BlendInTime = InBlendInTime;
  BlendOutTime = InBlendOutTime;
  RotationAmplitude = InRotationAmplitude;
  RotationFrequency = InRotationFrequency;
  LocationAmplitude = InLocationAmplitude;
  LocationFrequency = InLocationFrequency;
}

void USkaldOscillationCameraShake::StartShake(
    const FCameraShakeStartParams &Params) {
  Super::StartShake(Params);

  ElapsedTime = 0.f;
  bIsActive = true;
}

void USkaldOscillationCameraShake::UpdateAndApplyCameraShake(
    float DeltaTime, float Alpha, FMinimalViewInfo &InOutPOV) {
  Super::UpdateAndApplyCameraShake(DeltaTime, Alpha, InOutPOV);

  if (!bIsActive) {
    return;
  }

  ElapsedTime += DeltaTime;

  const float Blend = ComputeBlendFactor(ElapsedTime, Duration, BlendInTime,
                                         BlendOutTime) * Alpha;
  if (Blend <= KINDA_SMALL_NUMBER) {
    return;
  }

  const float TwoPi = UE_TWO_PI;

  auto EvalSine = [ElapsedTime, TwoPi](float Frequency) {
    return FMath::Sin(ElapsedTime * Frequency * TwoPi);
  };

  const float PitchDelta =
      RotationAmplitude.Pitch * EvalSine(RotationFrequency.Pitch) * Blend;
  const float YawDelta =
      RotationAmplitude.Yaw * EvalSine(RotationFrequency.Yaw) * Blend;
  const float RollDelta =
      RotationAmplitude.Roll * EvalSine(RotationFrequency.Roll) * Blend;

  InOutPOV.Rotation.Pitch += PitchDelta;
  InOutPOV.Rotation.Yaw += YawDelta;
  InOutPOV.Rotation.Roll += RollDelta;

  const float XDelta = LocationAmplitude.X * EvalSine(LocationFrequency.X);
  const float YDelta = LocationAmplitude.Y * EvalSine(LocationFrequency.Y);
  const float ZDelta = LocationAmplitude.Z * EvalSine(LocationFrequency.Z);

  InOutPOV.Location += FVector(XDelta, YDelta, ZDelta) * Blend;

  if (ElapsedTime >= Duration) {
    bIsActive = false;
  }
}

void USkaldOscillationCameraShake::StopShake(
    const FCameraShakeStopParams &Params) {
  Super::StopShake(Params);
  bIsActive = false;
}

bool USkaldOscillationCameraShake::IsFinished() const {
  return !bIsActive || ElapsedTime >= Duration;
}

USkaldHitCameraShake::USkaldHitCameraShake() {
  ConfigureShake(
      /*Duration*/ 0.2f,
      /*BlendInTime*/ 0.04f,
      /*BlendOutTime*/ 0.12f,
      /*RotationAmplitude*/ FRotator(0.75f, 0.6f, 0.f),
      /*RotationFrequency*/ FRotator(30.f, 26.f, 0.f),
      /*LocationAmplitude*/ FVector(0.f, 0.f, 1.5f),
      /*LocationFrequency*/ FVector(0.f, 0.f, 22.f));
}

USkaldMissCameraShake::USkaldMissCameraShake() {
  ConfigureShake(
      /*Duration*/ 0.12f,
      /*BlendInTime*/ 0.03f,
      /*BlendOutTime*/ 0.08f,
      /*RotationAmplitude*/ FRotator(0.35f, 0.3f, 0.f),
      /*RotationFrequency*/ FRotator(22.f, 20.f, 0.f),
      /*LocationAmplitude*/ FVector(0.f, 0.f, 0.75f),
      /*LocationFrequency*/ FVector(0.f, 0.f, 18.f));
}
