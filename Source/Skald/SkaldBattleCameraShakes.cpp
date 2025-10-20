#include "SkaldBattleCameraShakes.h"
#include "Math/UnrealMathUtility.h"

namespace
{
    static float ComputeBlendFactor(float Time, float Duration, float BlendInTime, float BlendOutTime)
    {
        if (Duration <= 0.f)
        {
            return 0.f;
        }

        const float RemainingTime = Duration - Time;
        float Blend = 1.f;

        if (BlendInTime > 0.f)
        {
            Blend = FMath::Min(Blend, FMath::Clamp(Time / BlendInTime, 0.f, 1.f));
        }

        if (BlendOutTime > 0.f)
        {
            Blend = FMath::Min(Blend, FMath::Clamp(RemainingTime / BlendOutTime, 0.f, 1.f));
        }

        return Blend;
    }
} // namespace

USkaldOscillationCameraShake::USkaldOscillationCameraShake(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , Duration(0.2f)
    , BlendInTime(0.05f)
    , BlendOutTime(0.05f)
    , RotationAmplitude(ForceInitToZero)
    , RotationFrequency(ForceInitToZero)
    , LocationAmplitude(ForceInitToZero)
    , LocationFrequency(ForceInitToZero)
    , ElapsedTime(0.f)
    , bIsActive(false)
{
}

void USkaldOscillationCameraShake::ConfigureShake(
    float InDuration, float InBlendInTime, float InBlendOutTime,
    const FRotator& InRotationAmplitude, const FRotator& InRotationFrequency,
    const FVector& InLocationAmplitude, const FVector& InLocationFrequency)
{
    Duration           = InDuration;
    BlendInTime        = InBlendInTime;
    BlendOutTime       = InBlendOutTime;
    RotationAmplitude  = InRotationAmplitude;
    RotationFrequency  = InRotationFrequency;
    LocationAmplitude  = InLocationAmplitude;
    LocationFrequency  = InLocationFrequency;
}

void USkaldOscillationCameraShake::StartShake(const FCameraShakeBaseStartParams& Params)
{
    Super::StartShake(Params);
    ElapsedTime = 0.f;
    bIsActive   = true;
}

void USkaldOscillationCameraShake::UpdateAndApplyCameraShake(
    const FCameraShakeBaseUpdateParams& Params,
    FCameraShakeBaseUpdateResult& OutResult)
{
    OutResult = FCameraShakeBaseUpdateResult(); // reset
    OutResult.LocationOffset = FVector::ZeroVector;
    OutResult.RotationOffset = FRotator::ZeroRotator;
    OutResult.FOV = 0.f;

    if (!bIsActive)
    {
        return;
    }

    ElapsedTime += Params.DeltaTime;

    const float Blend = ComputeBlendFactor(ElapsedTime, Duration, BlendInTime, BlendOutTime);
    if (Blend <= KINDA_SMALL_NUMBER)
    {
        if (ElapsedTime >= Duration)
        {
            bIsActive = false;
        }
        return;
    }

    const float TwoPi = UE_TWO_PI;

    auto EvalSine = [ElapsedTime = ElapsedTime, TwoPi](float Frequency)
    {
        return FMath::Sin(ElapsedTime * Frequency * TwoPi);
    };

    OutResult.LocationOffset =
        FVector(LocationAmplitude.X * EvalSine(LocationFrequency.X),
                LocationAmplitude.Y * EvalSine(LocationFrequency.Y),
                LocationAmplitude.Z * EvalSine(LocationFrequency.Z)) * Blend;

    OutResult.RotationOffset =
        FRotator(RotationAmplitude.Pitch * EvalSine(RotationFrequency.Pitch),
                 RotationAmplitude.Yaw   * EvalSine(RotationFrequency.Yaw),
                 RotationAmplitude.Roll  * EvalSine(RotationFrequency.Roll)) * Blend;

    OutResult.FOV = 0.f;

    if (ElapsedTime >= Duration)
    {
        bIsActive = false;
    }
}

void USkaldOscillationCameraShake::StopShake(bool bImmediately)
{
    Super::StopShake(bImmediately);
    bIsActive = false;
}

bool USkaldOscillationCameraShake::IsFinished() const
{
    return !bIsActive || ElapsedTime >= Duration;
}

USkaldHitCameraShake::USkaldHitCameraShake(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    ConfigureShake(
        /*Duration*/        0.20f,
        /*BlendInTime*/     0.04f,
        /*BlendOutTime*/    0.12f,
        /*RotationAmplitude*/ FRotator(0.75f, 0.60f, 0.0f),
        /*RotationFrequency*/ FRotator(30.0f, 26.0f, 0.0f),
        /*LocationAmplitude*/ FVector(0.0f, 0.0f, 1.5f),
        /*LocationFrequency*/ FVector(0.0f, 0.0f, 22.0f));
}

USkaldMissCameraShake::USkaldMissCameraShake(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    ConfigureShake(
        /*Duration*/        0.12f,
        /*BlendInTime*/     0.03f,
        /*BlendOutTime*/    0.08f,
        /*RotationAmplitude*/ FRotator(0.35f, 0.30f, 0.0f),
        /*RotationFrequency*/ FRotator(22.0f, 20.0f, 0.0f),
        /*LocationAmplitude*/ FVector(0.0f, 0.0f, 0.75f),
        /*LocationFrequency*/ FVector(0.0f, 0.0f, 18.0f));
}

