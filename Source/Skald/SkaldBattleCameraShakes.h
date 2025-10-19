#pragma once

#include "Camera/MatineeCameraShake.h"
#include "SkaldBattleCameraShakes.generated.h"

/**
 * Lightweight micro shake triggered when a battle attack successfully hits.
 */
UCLASS()
class SKALD_API USkaldHitCameraShake : public UMatineeCameraShake {
  GENERATED_BODY()

public:
  USkaldHitCameraShake();
};

/**
 * Even subtler shake used for near-miss feedback to keep motion comfortable.
 */
UCLASS()
class SKALD_API USkaldMissCameraShake : public UMatineeCameraShake {
  GENERATED_BODY()

public:
  USkaldMissCameraShake();
};

