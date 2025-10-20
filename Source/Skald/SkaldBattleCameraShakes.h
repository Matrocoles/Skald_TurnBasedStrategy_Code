#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"
#include "SkaldBattleCameraShakes.generated.h"

#if __has_include("Camera/CameraShakeBaseTypes.h")
#include "Camera/CameraShakeBaseTypes.h"
#else
using FCameraShakeStartParams = FCameraShakeBaseStartParams;
using FCameraShakeUpdateParams = FCameraShakeBaseUpdateParams;
using FCameraShakeUpdateResult = FCameraShakeBaseUpdateResult;
using FCameraShakeStopParams = FCameraShakeBaseStopParams;
#endif

/**
 * Minimal oscillating shake built on top of the UE 5.5 camera shake base class.
 */
UCLASS(Abstract)
class SKALD_API USkaldOscillationCameraShake : public UCameraShakeBase {
  GENERATED_BODY()

public:
  explicit USkaldOscillationCameraShake(
      const FObjectInitializer &ObjectInitializer = FObjectInitializer::Get());

protected:
  virtual void StartShake(const FCameraShakeStartParams &Params) override;
  virtual void UpdateAndApplyCameraShake(
      const FCameraShakeUpdateParams &Params,
      FCameraShakeUpdateResult &OutResult) override;
  virtual void StopShake(const FCameraShakeStopParams &Params) override;
  virtual bool IsFinished() const override;

  void ConfigureShake(float InDuration, float InBlendInTime,
                      float InBlendOutTime,
                      const FRotator &InRotationAmplitude,
                      const FRotator &InRotationFrequency,
                      const FVector &InLocationAmplitude,
                      const FVector &InLocationFrequency);

protected:
  float Duration;
  float BlendInTime;
  float BlendOutTime;
  FRotator RotationAmplitude;
  FRotator RotationFrequency;
  FVector LocationAmplitude;
  FVector LocationFrequency;

private:
  float ElapsedTime;
  bool bIsActive;
};

/**
 * Lightweight micro shake triggered when a battle attack successfully hits.
 */
UCLASS()
class SKALD_API USkaldHitCameraShake : public USkaldOscillationCameraShake {
  GENERATED_BODY()

public:
  explicit USkaldHitCameraShake(
      const FObjectInitializer &ObjectInitializer = FObjectInitializer::Get());
};

/**
 * Even subtler shake used for near-miss feedback to keep motion comfortable.
 */
UCLASS()
class SKALD_API USkaldMissCameraShake : public USkaldOscillationCameraShake {
  GENERATED_BODY()

public:
  explicit USkaldMissCameraShake(
      const FObjectInitializer &ObjectInitializer = FObjectInitializer::Get());
};
