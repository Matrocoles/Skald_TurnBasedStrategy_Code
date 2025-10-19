#pragma once

#include "Camera/CameraShakeBase.h"
#include "OscillatorCameraShakePattern.h"
#include "SkaldBattleCameraShakes.generated.h"

/**
 * Lightweight micro shake triggered when a battle attack successfully hits.
 */
UCLASS()
class SKALD_API USkaldHitCameraShake : public UCameraShakeBase {
  GENERATED_BODY()

public:
  USkaldHitCameraShake();

private:
  UPROPERTY(VisibleAnywhere, Instanced, Category = "Camera Shake")
  TObjectPtr<UOscillatorCameraShakePattern> OscillationPattern;
};

/**
 * Even subtler shake used for near-miss feedback to keep motion comfortable.
 */
UCLASS()
class SKALD_API USkaldMissCameraShake : public UCameraShakeBase {
  GENERATED_BODY()

public:
  USkaldMissCameraShake();

private:
  UPROPERTY(VisibleAnywhere, Instanced, Category = "Camera Shake")
  TObjectPtr<UOscillatorCameraShakePattern> OscillationPattern;
};

