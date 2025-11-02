#include "DiceRollArena.h"
#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "DiceRollConfig.h"

ADiceRollArena::ADiceRollArena()
{
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
    Bounds->SetupAttachment(RootComponent);
    Bounds->SetBoxExtent(FVector(100.f, 100.f, 100.f));
    Bounds->SetCollisionProfileName(TEXT("BlockAll"));

    FloorVisual = CreateDefaultSubobject<UDecalComponent>(TEXT("Floor"));
    FloorVisual->SetupAttachment(RootComponent);
    FloorVisual->DecalSize = FVector(100.f, 128.f, 128.f);

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
    }
}
