#include "SkaldDiceD6.h"

#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsSettings.h"

ASkaldDiceD6::ASkaldDiceD6()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    DiceMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DiceMesh"));
    RootComponent = DiceMesh;
    DiceMesh->SetSimulatePhysics(true);
    DiceMesh->SetNotifyRigidBodyCollision(true);
    DiceMesh->SetEnableGravity(true);
    DiceMesh->BodyInstance.bUseCCD = true;

    ImpactAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("ImpactAudio"));
    ImpactAudio->SetupAttachment(RootComponent);
    ImpactAudio->bAutoActivate = false;
}

void ASkaldDiceD6::BeginPlay()
{
    Super::BeginPlay();
    SettleTimer = 0.f;
    bSettled = false;
    bBroadcastSettlement = false;
    ResolvedValue = 1;
}

void ASkaldDiceD6::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateSettleState(DeltaSeconds);
}

void ASkaldDiceD6::InitialiseFromConfig(const UDiceRollConfig* InConfig)
{
    CachedConfig = InConfig;
    SettleTimer = 0.f;
    bSettled = false;
    bBroadcastSettlement = false;
    ResolvedValue = 1;
}

void ASkaldDiceD6::SetDesiredFaceValue(int32 InDesiredValue)
{
    DesiredValue = InDesiredValue;
}

void ASkaldDiceD6::ApplyTint(const FLinearColor& Tint, FName TintParameterName)
{
    if (!DiceMesh)
    {
        return;
    }

    if (!TintParameterName.IsNone())
    {
        EnsureDynamicMaterialsCreated();
        for (UMaterialInstanceDynamic* MaterialInstance : DynamicMaterials)
        {
            if (MaterialInstance)
            {
                MaterialInstance->SetVectorParameterValue(TintParameterName, Tint);
            }
        }
    }
    else
    {
        DiceMesh->SetVectorParameterValueOnMaterials(TEXT("TintColor"), Tint.ToFVector3());
    }
}

void ASkaldDiceD6::LaunchDie(const FVector& Position, const FRotator& Rotation, const FVector& LinearImpulse, const FVector& AngularImpulse)
{
    if (!DiceMesh)
    {
        return;
    }

    const FTransform SpawnTransform(Rotation, Position);
    SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);

    DiceMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
    DiceMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    DiceMesh->SetSimulatePhysics(true);
    DiceMesh->WakeAllRigidBodies();

    if (!LinearImpulse.IsNearlyZero())
    {
        DiceMesh->AddImpulse(LinearImpulse);
    }

    if (!AngularImpulse.IsNearlyZero())
    {
        DiceMesh->AddAngularImpulseInDegrees(AngularImpulse);
    }

    bSettled = false;
    bBroadcastSettlement = false;
    SettleTimer = 0.f;
}

void ASkaldDiceD6::ForceFaceValue(int32 TargetValue, bool bBroadcastResult)
{
    if (!DiceMesh)
    {
        return;
    }

    int32 Resolved = TargetValue;
    if (Resolved <= 0 && DesiredValue > 0)
    {
        Resolved = DesiredValue;
    }

    if (Resolved <= 0)
    {
        Resolved = ResolveFaceValue();
    }

    const FSkaldDiceFaceMapping* Mapping = nullptr;
    if (CachedConfig)
    {
        for (const FSkaldDiceFaceMapping& Entry : CachedConfig->FaceLookup)
        {
            if (Entry.FaceValue == Resolved)
            {
                Mapping = &Entry;
                break;
            }
        }
    }

    FVector DesiredUp = FVector::UpVector;
    if (Mapping)
    {
        DesiredUp = Mapping->UpNormal.GetSafeNormal();
    }

    const FRotator TargetRot = FRotationMatrix::MakeFromZ(DesiredUp).Rotator() + FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f);
    const FVector Location = GetActorLocation();

    DiceMesh->SetSimulatePhysics(false);
    DiceMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
    DiceMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    SetActorLocationAndRotation(Location, TargetRot, false, nullptr, ETeleportType::TeleportPhysics);
    DiceMesh->SetWorldRotation(TargetRot, false, nullptr, ETeleportType::TeleportPhysics);

    bSettled = true;
    ResolvedValue = FMath::Clamp(Resolved, 1, 6);
    if (bBroadcastResult)
    {
        if (!bBroadcastSettlement)
        {
            bBroadcastSettlement = true;
            OnDiceSettled.Broadcast(this, ResolvedValue);
        }
        else
        {
            OnDiceSettled.Broadcast(this, ResolvedValue);
        }
    }
    else
    {
        bBroadcastSettlement = true;
    }
}

int32 ASkaldDiceD6::SampleFaceValue() const
{
    if (bSettled)
    {
        return ResolvedValue;
    }
    return ResolveFaceValue();
}

void ASkaldDiceD6::UpdateSettleState(float DeltaSeconds)
{
    if (bSettled || !DiceMesh)
    {
        return;
    }

    const FVector LinearVelocity = DiceMesh->GetPhysicsLinearVelocity();
    const FVector AngularVelocity = DiceMesh->GetPhysicsAngularVelocityInDegrees();

    const float VelocityThreshold = CachedConfig ? CachedConfig->SettleVelocityThreshold : 2.5f;
    const float AngularThreshold = CachedConfig ? CachedConfig->SettleAngularThreshold : 10.f;
    const float HoldTime = CachedConfig ? CachedConfig->SettleHoldTime : 0.3f;

    if (LinearVelocity.Size() < VelocityThreshold && AngularVelocity.Size() < AngularThreshold)
    {
        SettleTimer += DeltaSeconds;
    }
    else
    {
        SettleTimer = 0.f;
    }

    if (SettleTimer >= HoldTime)
    {
        bSettled = true;
        ResolvedValue = ResolveFaceValue();
        if (DesiredValue > 0 && ResolvedValue != DesiredValue)
        {
            ForceFaceValue(DesiredValue, true);
            return;
        }

        if (!bBroadcastSettlement)
        {
            bBroadcastSettlement = true;
            OnDiceSettled.Broadcast(this, ResolvedValue);
        }
    }
}

int32 ASkaldDiceD6::ResolveFaceValue() const
{
    if (!DiceMesh)
    {
        return 1;
    }

    const FVector UpVector = DiceMesh->GetComponentTransform().GetUnitAxis(EAxis::Z);

    int32 BestValue = 1;
    float BestScore = -1.f;

    if (CachedConfig && CachedConfig->FaceLookup.Num() > 0)
    {
        for (const FSkaldDiceFaceMapping& Mapping : CachedConfig->FaceLookup)
        {
            const float Score = FVector::DotProduct(UpVector, Mapping.UpNormal.GetSafeNormal());
            if (Score > BestScore)
            {
                BestScore = Score;
                BestValue = Mapping.FaceValue;
            }
        }
    }
    else
    {
        const FVector WorldUp = FVector::UpVector;
        const float Alignment = FVector::DotProduct(UpVector, WorldUp);
        BestValue = Alignment > 0.f ? 1 : 6;
    }

    return FMath::Clamp(BestValue, 1, 6);
}

void ASkaldDiceD6::EnsureDynamicMaterialsCreated()
{
    if (!DiceMesh)
    {
        return;
    }

    if (DynamicMaterials.Num() > 0)
    {
        return;
    }

    const int32 MaterialCount = DiceMesh->GetNumMaterials();
    for (int32 Index = 0; Index < MaterialCount; ++Index)
    {
        if (UMaterialInterface* Material = DiceMesh->GetMaterial(Index))
        {
            if (UMaterialInstanceDynamic* DynamicMaterial = DiceMesh->CreateDynamicMaterialInstance(Index, Material))
            {
                DynamicMaterials.Add(DynamicMaterial);
            }
        }
    }
}
