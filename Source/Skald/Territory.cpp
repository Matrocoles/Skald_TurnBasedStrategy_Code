#include "Territory.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Skald_PlayerState.h"
#include "Skald_PlayerController.h"
#include "SkaldTypes.h"
#include "Net/UnrealNetwork.h"

namespace
{
FLinearColor GetFactionColor(ESkaldFaction Faction)
{
  switch (Faction)
  {
  case ESkaldFaction::Human: return FLinearColor::Blue;
  case ESkaldFaction::Orc: return FLinearColor::Green;
  case ESkaldFaction::Dwarf: return FLinearColor(0.6f, 0.6f, 0.6f, 1.f);
  case ESkaldFaction::Elf: return FLinearColor(0.0f, 0.8f, 0.4f, 1.f);
  case ESkaldFaction::LizardFolk: return FLinearColor(0.0f, 0.6f, 0.3f, 1.f);
  case ESkaldFaction::Undead: return FLinearColor(0.5f, 0.0f, 0.5f, 1.f);
  case ESkaldFaction::Gnoll: return FLinearColor(0.8f, 0.4f, 0.0f, 1.f);
  case ESkaldFaction::Empire: return FLinearColor::Red;
  default: return FLinearColor::White;
  }
}
}

ATerritory::ATerritory() {
  PrimaryActorTick.bCanEverTick = false;
  bReplicates = true;
  MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
  RootComponent = MeshComponent;

  OwningPlayer = nullptr;
  Resources = 0;
  TerritoryID = 0;
  TerritoryName = TEXT("");
  bIsCapital = false;
  ContinentID = 0;
  ArmyStrength = 0;
}

void ATerritory::BeginPlay() {
  Super::BeginPlay();

  if (MeshComponent) {
    MeshComponent->OnBeginCursorOver.AddDynamic(this,
                                                &ATerritory::HandleMouseEnter);
    MeshComponent->OnEndCursorOver.AddDynamic(this,
                                              &ATerritory::HandleMouseLeave);
    MeshComponent->OnClicked.AddDynamic(this, &ATerritory::HandleClicked);

    if (MeshComponent->GetNumMaterials() > 0) {
      DynamicMaterial = MeshComponent->CreateAndSetMaterialInstanceDynamic(0);
      if (DynamicMaterial) {
        DynamicMaterial->GetVectorParameterValue(FName("Color"), DefaultColor);
      }
    } else {
      UE_LOG(LogTemp, Warning,
             TEXT("Territory %s has no material at index 0"), *GetName());
    }
  }

  UpdateTerritoryColor();

  // Territories are registered with the world map immediately after
  // spawning, so no self-registration is required here.
}

void ATerritory::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  DOREPLIFETIME(ATerritory, OwningPlayer);
  DOREPLIFETIME(ATerritory, Resources);
  DOREPLIFETIME(ATerritory, TerritoryID);
  DOREPLIFETIME(ATerritory, TerritoryName);
  DOREPLIFETIME(ATerritory, bIsCapital);
  DOREPLIFETIME(ATerritory, ContinentID);
  DOREPLIFETIME(ATerritory, AdjacentTerritories);
  DOREPLIFETIME(ATerritory, ArmyStrength);
}

void ATerritory::Select() {
  if (bIsSelected) {
    return;
  }

  bIsSelected = true;
  if (DynamicMaterial) {
    DynamicMaterial->SetVectorParameterValue(FName("Color"),
                                             FLinearColor::Yellow);
  }
  OnTerritorySelected.Broadcast(this);
  if (ASkaldPlayerController* PC =
          Cast<ASkaldPlayerController>(GetWorld()->GetFirstPlayerController())) {
    PC->HandleTerritorySelected(this);
  }
}

void ATerritory::Deselect() {
  bIsSelected = false;
  if (DynamicMaterial) {
    DynamicMaterial->SetVectorParameterValue(FName("Color"), DefaultColor);
  }
}

bool ATerritory::IsAdjacentTo(const ATerritory *Other) const {
  return AdjacentTerritories.Contains(Other);
}

bool ATerritory::MoveTo(ATerritory *TargetTerritory) {
  if (!TargetTerritory || !IsAdjacentTo(TargetTerritory)) {
    return false;
  }

  // Movement logic would be handled here. For now we simply select the target.
  // Movement logic would be handled here. For now we simply deselect this
  // territory and select the target.
  Deselect();
  TargetTerritory->Select();
  return true;
}

void ATerritory::HandleMouseEnter(UPrimitiveComponent *TouchedComponent) {
  if (DynamicMaterial && !bIsSelected) {
    DynamicMaterial->SetVectorParameterValue(FName("Color"),
                                             FLinearColor::White);
  }
}

void ATerritory::HandleMouseLeave(UPrimitiveComponent *TouchedComponent) {
  if (DynamicMaterial && !bIsSelected) {
    DynamicMaterial->SetVectorParameterValue(FName("Color"), DefaultColor);
  }
}

void ATerritory::HandleClicked(UPrimitiveComponent *TouchedComponent,
                               FKey ButtonPressed) {
  if (!bIsSelected) {
    Select();
  } else {
    Deselect();
  }
}

void ATerritory::RefreshAppearance() { UpdateTerritoryColor(); }

void ATerritory::OnRep_OwningPlayer() { UpdateTerritoryColor(); }

void ATerritory::UpdateTerritoryColor() {
  if (DynamicMaterial) {
    FLinearColor NewColor = DefaultColor;
    if (OwningPlayer) {
      NewColor = GetFactionColor(OwningPlayer->Faction);
    }
    DynamicMaterial->SetVectorParameterValue(FName("Color"), NewColor);
  }
}
