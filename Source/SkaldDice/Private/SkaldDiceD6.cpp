#include "SkaldDiceD6.h"

#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "SkaldDiceModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "UObject/ConstructorHelpers.h"

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
    DiceMesh->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (DefaultMeshFinder.Succeeded())
    {
        DiceMesh->SetStaticMesh(DefaultMeshFinder.Object);
        DiceMesh->SetRelativeScale3D(FVector(0.35f));
    }

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (DefaultMaterialFinder.Succeeded())
    {
        DiceMesh->SetMaterial(0, DefaultMaterialFinder.Object);
    }

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
    bSnapToDesiredValue = true;

    ValidateFaceMappings();
}

void ASkaldDiceD6::SetDesiredFaceValue(int32 InDesiredValue)
{
    DesiredValue = InDesiredValue;
}

void ASkaldDiceD6::SetShouldSnapToDesired(bool bEnable)
{
    bSnapToDesiredValue = bEnable;
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
        const FVector TintVector(Tint.R, Tint.G, Tint.B);
        DiceMesh->SetVectorParameterValueOnMaterials(TEXT("TintColor"), TintVector);
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

    FQuat TargetQuat = FQuat::Identity;
    if (Mapping)
    {
        const FVector MappingLocalNormal = GetFaceLocalNormal(*Mapping);
        if (!MappingLocalNormal.IsNearlyZero())
        {
            const FVector NormalisedLocalNormal = MappingLocalNormal.GetSafeNormal();
            TargetQuat = FQuat::FindBetweenNormals(NormalisedLocalNormal, FVector::UpVector);
        }
    }

    const float RandomYaw = FMath::FRandRange(0.f, 360.f);
    if (!FMath::IsNearlyZero(RandomYaw))
    {
        const FQuat YawQuat(FVector::UpVector, FMath::DegreesToRadians(RandomYaw));
        TargetQuat = YawQuat * TargetQuat;
    }

    if (TargetQuat.ContainsNaN())
    {
        TargetQuat = FQuat::Identity;
    }

    const FRotator TargetRot = TargetQuat.Rotator();
    const FVector Location = GetActorLocation();

    DiceMesh->SetSimulatePhysics(false);
    DiceMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
    DiceMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    SetActorLocationAndRotation(Location, TargetRot, false, nullptr, ETeleportType::TeleportPhysics);
    DiceMesh->SetWorldRotation(TargetRot, false, nullptr, ETeleportType::TeleportPhysics);

    StabiliseAfterSettlement();

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
            if (bSnapToDesiredValue)
            {
                ForceFaceValue(DesiredValue, true);
                return;
            }

            ResolvedValue = FMath::Clamp(DesiredValue, 1, 6);
        }

        StabiliseAfterSettlement();

        if (!bBroadcastSettlement)
        {
            bBroadcastSettlement = true;
            OnDiceSettled.Broadcast(this, ResolvedValue);
        }
    }
}

void ASkaldDiceD6::StabiliseAfterSettlement()
{
    if (!DiceMesh)
    {
        return;
    }

    DiceMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
    DiceMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    DiceMesh->PutRigidBodyToSleep();
    DiceMesh->SetSimulatePhysics(false);
}

int32 ASkaldDiceD6::ResolveFaceValue() const
{
    if (!DiceMesh)
    {
        return 1;
    }

    const FVector ComponentUp = DiceMesh->GetComponentTransform().GetUnitAxis(EAxis::Z);

    int32 BestValue = 1;
    float BestScore = -2.f;
    bool bFoundFace = false;

    if (CachedConfig && CachedConfig->FaceLookup.Num() > 0)
    {
        for (const FSkaldDiceFaceMapping& Mapping : CachedConfig->FaceLookup)
        {
            const FVector FaceNormal = GetFaceWorldNormal(Mapping);
            if (FaceNormal.IsNearlyZero())
            {
                continue;
            }

            const float Score = FVector::DotProduct(FaceNormal, FVector::UpVector);
            if (Score > 0.f && Score > BestScore)
            {
                BestScore = Score;
                BestValue = Mapping.FaceValue;
                bFoundFace = true;
            }
        }
    }
    else
    {
        const FVector WorldUp = FVector::UpVector;
        const float Alignment = FVector::DotProduct(ComponentUp, WorldUp);
        BestValue = Alignment > 0.f ? 1 : 6;
    }

    if (!bFoundFace)
    {
        const float Alignment = FVector::DotProduct(ComponentUp, FVector::UpVector);
        BestValue = Alignment > 0.f ? 1 : 6;
    }

    return FMath::Clamp(BestValue, 1, 6);
}

FVector ASkaldDiceD6::GetFaceLocalNormal(const FSkaldDiceFaceMapping& Mapping) const
{
    if (!DiceMesh)
    {
        return FVector::ZeroVector;
    }

    if (Mapping.SocketName != NAME_None && DiceMesh->DoesSocketExist(Mapping.SocketName))
    {
        const FTransform SocketTransform = DiceMesh->GetSocketTransform(Mapping.SocketName, ERelativeTransformSpace::RTS_Component);
        return SocketTransform.GetUnitAxis(EAxis::Z).GetSafeNormal();
    }

    const FVector LocalNormal = Mapping.LocalNormal;
    if (!LocalNormal.IsNearlyZero())
    {
        return LocalNormal.GetSafeNormal();
    }

    return FVector::ZeroVector;
}

FVector ASkaldDiceD6::GetFaceWorldNormal(const FSkaldDiceFaceMapping& Mapping) const
{
    if (!DiceMesh)
    {
        return FVector::ZeroVector;
    }

    const FVector LocalNormal = GetFaceLocalNormal(Mapping);
    if (!LocalNormal.IsNearlyZero())
    {
        return DiceMesh->GetComponentTransform().TransformVectorNoScale(LocalNormal).GetSafeNormal();
    }

    return FVector::ZeroVector;
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

void ASkaldDiceD6::ValidateFaceMappings()
{
    if (bHasValidatedFaceMappings)
    {
        return;
    }

    bHasValidatedFaceMappings = true;

    if (!DiceMesh || !CachedConfig)
    {
        return;
    }

    const UStaticMesh* StaticMesh = DiceMesh->GetStaticMesh();

    const TArray<FName> MeshSocketNames = DiceMesh->GetAllSocketNames();

    FString AvailableSockets;
    if (MeshSocketNames.Num() > 0)
    {
        TArray<FString> SocketStrings;
        SocketStrings.Reserve(MeshSocketNames.Num());
        for (const FName& SocketName : MeshSocketNames)
        {
            SocketStrings.Add(SocketName.ToString());
        }

        AvailableSockets = FString::Join(SocketStrings, TEXT(", "));
    }
    else
    {
        AvailableSockets = TEXT("<none>");
    }

    for (const FSkaldDiceFaceMapping& Mapping : CachedConfig->FaceLookup)
    {
        const bool bHasSocket = Mapping.SocketName != NAME_None;
        const bool bSocketValid = bHasSocket && DiceMesh->DoesSocketExist(Mapping.SocketName);
        const bool bHasLocalNormal = !Mapping.LocalNormal.IsNearlyZero();

        if (bHasSocket && !bSocketValid)
        {
            UE_LOG(LogSkaldDice, Warning, TEXT("Dice '%s' (mesh '%s') references missing socket '%s' for face value %d. Available sockets: %s"),
                *GetNameSafe(this),
                *GetNameSafe(StaticMesh),
                *Mapping.SocketName.ToString(),
                Mapping.FaceValue,
                *AvailableSockets);
        }

        if (!bSocketValid && !bHasLocalNormal)
        {
            UE_LOG(LogSkaldDice, Warning, TEXT("Dice '%s' (mesh '%s') face value %d has neither a valid socket nor a non-zero local normal; falling back to heuristics."),
                *GetNameSafe(this),
                *GetNameSafe(StaticMesh),
                Mapping.FaceValue);
        }
    }
}
