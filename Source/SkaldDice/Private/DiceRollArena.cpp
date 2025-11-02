#include "DiceRollArena.h"
#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DiceRollConfig.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ADiceRollArena::ADiceRollArena()
{
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
    Bounds->SetupAttachment(RootComponent);
    Bounds->SetBoxExtent(FVector(100.f, 100.f, 100.f));
    Bounds->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
    Bounds->SetGenerateOverlapEvents(false);

    FloorVisual = CreateDefaultSubobject<UDecalComponent>(TEXT("Floor"));
    FloorVisual->SetupAttachment(RootComponent);
    FloorVisual->DecalSize = FVector(100.f, 128.f, 128.f);

    FloorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FloorMesh"));
    FloorMesh->SetupAttachment(RootComponent);
    FloorMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
    FloorMesh->SetGenerateOverlapEvents(false);
    FloorMesh->SetSimulatePhysics(false);
    FloorMesh->SetEnableGravity(false);
    FloorMesh->SetMobility(EComponentMobility::Movable);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMeshFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (PlaneMeshFinder.Succeeded())
    {
        FloorMesh->SetStaticMesh(PlaneMeshFinder.Object);
    }

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlaneMaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (PlaneMaterialFinder.Succeeded())
    {
        FloorMesh->SetMaterial(0, PlaneMaterialFinder.Object);
    }

    KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
    KeyLight->SetupAttachment(RootComponent);
    KeyLight->Intensity = 5000.f;
    KeyLight->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
}

void ADiceRollArena::ConfigureArena(const UDiceRollConfig* Config)
{
    if (!Config || !Bounds)
    {
        return;
    }

    const FVector Extent = Config->ArenaBounds.GetExtent();
    Bounds->SetBoxExtent(Extent);
    const FVector Center = Config->ArenaBounds.GetCenter();
    SetActorLocation(Center);

    if (FloorVisual)
    {
        FloorVisual->DecalSize = FVector(Extent.Z, Extent.X, Extent.Y);
        FloorVisual->SetRelativeLocation(FVector(0.f, 0.f, -Extent.Z + 1.f));
    }

    if (FloorMesh)
    {
        const float SafeScale = 0.01f;
        const float ScaleX = FMath::Max((Extent.X * 2.f) / 100.f, SafeScale);
        const float ScaleY = FMath::Max((Extent.Y * 2.f) / 100.f, SafeScale);
        FloorMesh->SetRelativeLocation(FVector(0.f, 0.f, -Extent.Z));
        FloorMesh->SetRelativeScale3D(FVector(ScaleX, ScaleY, 1.f));
    }
}
