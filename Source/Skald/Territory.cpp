#include "Territory.h"
#include "Skald_PlayerState.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PrimitiveComponent.h"

ATerritory::ATerritory()
{
    PrimaryActorTick.bCanEverTick = false;
    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    RootComponent = MeshComponent;

    OwningPlayer = nullptr;
    Resources = 0;
    TerritoryID = 0;
    ArmyStrength = 0;
}

void ATerritory::BeginPlay()
{
    Super::BeginPlay();

    if (MeshComponent)
    {
        MeshComponent->OnBeginCursorOver.AddDynamic(this, &ATerritory::HandleMouseEnter);
        MeshComponent->OnEndCursorOver.AddDynamic(this, &ATerritory::HandleMouseLeave);
        MeshComponent->OnClicked.AddDynamic(this, &ATerritory::HandleClicked);

        DynamicMaterial = MeshComponent->CreateAndSetMaterialInstanceDynamic(0);
        if (DynamicMaterial)
        {
            DynamicMaterial->GetVectorParameterValue(FName("Color"), DefaultColor);
        }
    }
}

void ATerritory::Select()
{
    bIsSelected = true;
    if (DynamicMaterial)
    {
        DynamicMaterial->SetVectorParameterValue(FName("Color"), FLinearColor::Yellow);
    }
    OnTerritorySelected.Broadcast(this);
}

bool ATerritory::IsAdjacentTo(const ATerritory* Other) const
{
    for (const ATerritory* Adjacent : AdjacentTerritories)
    {
        if (Adjacent == Other)
        {
            return true;
        }
    }
    return false;
}

bool ATerritory::MoveTo(ATerritory* TargetTerritory)
{
    if (!TargetTerritory || !IsAdjacentTo(TargetTerritory))
    {
        return false;
    }

    // Movement logic would be handled here. For now we simply select the target.
    TargetTerritory->Select();
    return true;
}

void ATerritory::HandleMouseEnter(UPrimitiveComponent* TouchedComponent)
{
    if (DynamicMaterial && !bIsSelected)
    {
        DynamicMaterial->SetVectorParameterValue(FName("Color"), FLinearColor::White);
    }
}

void ATerritory::HandleMouseLeave(UPrimitiveComponent* TouchedComponent)
{
    if (DynamicMaterial && !bIsSelected)
    {
        DynamicMaterial->SetVectorParameterValue(FName("Color"), DefaultColor);
    }
}

void ATerritory::HandleClicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed)
{
    Select();
}

