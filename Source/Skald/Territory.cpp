#include "Territory.h"
#include "Skald.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "SkaldTypes.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "UObject/ConstructorHelpers.h"

namespace {
FLinearColor GetFactionColor(ESkaldFaction Faction) {
  switch (Faction) {
  case ESkaldFaction::Human:
    return FLinearColor::Blue;
  case ESkaldFaction::Orc:
    return FLinearColor::Red;
  case ESkaldFaction::Dwarf:
    return FLinearColor(0.6f, 0.3f, 0.0f, 1.f); // Brown
  case ESkaldFaction::Elf:
    return FLinearColor::Green;
  case ESkaldFaction::LizardFolk:
    return FLinearColor(0.0f, 0.5f, 0.5f, 1.f); // Teal
  case ESkaldFaction::Undead:
    return FLinearColor::Black;
  case ESkaldFaction::Gnoll:
    return FLinearColor(1.0f, 0.55f, 0.0f, 1.f); // Orange
  case ESkaldFaction::Empire:
    return FLinearColor(0.5f, 0.0f, 0.5f, 1.f); // Purple
  default:
    return FLinearColor::White;
  }
}
} // namespace

ATerritory::ATerritory() {
  PrimaryActorTick.bCanEverTick = false;
  bReplicates = true;
  MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
  RootComponent = MeshComponent;

  // Provide basic visuals so the world map can function even if assets
  // are missing in the editor. This avoids runtime errors about missing
  // meshes or materials when spawning territories.
  static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultMesh(
      TEXT("/Engine/BasicShapes/Sphere.Sphere"));
  if (DefaultMesh.Succeeded()) {
    MeshComponent->SetStaticMesh(DefaultMesh.Object);
  }
  static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMat(
      TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
  if (DefaultMat.Succeeded()) {
    MeshComponent->SetMaterial(0, DefaultMat.Object);
  }

  LabelComponent = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
  LabelComponent->SetupAttachment(RootComponent);
  LabelComponent->SetHorizontalAlignment(EHTA_Center);
  LabelComponent->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
  LabelComponent->SetText(FText::GetEmpty());

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

  if (!CapitalMesh) {
    CapitalMesh = NewObject<UStaticMeshComponent>(this, TEXT("CapitalMesh"));
    if (CapitalMesh) {
      CapitalMesh->SetupAttachment(RootComponent);
      if (CapitalMeshAsset) {
        CapitalMesh->SetStaticMesh(CapitalMeshAsset);
      }
      CapitalMesh->SetVisibility(false);
      CapitalMesh->SetHiddenInGame(true);
      CapitalMesh->RegisterComponent();
    }
  }

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
      UE_LOG(LogSkald, Warning, TEXT("Territory %s has no material at index 0"),
             *GetName());
    }

    // Ensure borders are not highlighted by default
    MeshComponent->SetRenderCustomDepth(false);
  }

  UpdateTerritoryColor();
  UpdateLabel();

  if (CapitalMesh) {
    CapitalMesh->SetVisibility(bIsCapital);
    CapitalMesh->SetHiddenInGame(!bIsCapital);
  }

  // Territories are registered with the world map immediately after
  // spawning, so no self-registration is required here.
}

void ATerritory::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  DOREPLIFETIME(ATerritory, OwningPlayer);
  DOREPLIFETIME(ATerritory, Resources);
  DOREPLIFETIME(ATerritory, TerritoryID);
  DOREPLIFETIME(ATerritory, TerritoryName);
  DOREPLIFETIME(ATerritory, bIsCapital);
  DOREPLIFETIME(ATerritory, ContinentID);
  DOREPLIFETIME(ATerritory, AdjacentTerritories);
  DOREPLIFETIME(ATerritory, ArmyStrength);
  DOREPLIFETIME(ATerritory, BuiltSiegeID);
  DOREPLIFETIME(ATerritory, HasTreasure);
}

void ATerritory::Select() {
  if (bIsSelected) {
    return;
  }

  bIsSelected = true;
  if (MeshComponent) {
    MeshComponent->SetRenderCustomDepth(true);
  }
  if (DynamicMaterial) {
    // Remember the existing color so it can be restored on deselect
    DynamicMaterial->GetVectorParameterValue(FName("Color"), DefaultColor);
    DynamicMaterial->SetVectorParameterValue(FName("Color"),
                                             FLinearColor::White);
  }
  OnTerritorySelected.Broadcast(this);
}

void ATerritory::Deselect() {
  bIsSelected = false;
  if (MeshComponent) {
    MeshComponent->SetRenderCustomDepth(false);
  }
  if (DynamicMaterial) {
    // Restore the color that was in use prior to selection
    DynamicMaterial->SetVectorParameterValue(FName("Color"), DefaultColor);
  }
}

bool ATerritory::IsAdjacentTo(const ATerritory *Other) const {
  return AdjacentTerritories.Contains(Other);
}

bool ATerritory::MoveTo(ATerritory *TargetTerritory, int32 Troops) {
  if (!TargetTerritory || Troops <= 0 || Troops >= ArmyStrength) {
    return false;
  }

  if (!IsAdjacentTo(TargetTerritory) ||
      TargetTerritory->OwningPlayer != OwningPlayer) {
    return false;
  }

  ArmyStrength -= Troops;
  TargetTerritory->ArmyStrength += Troops;

  Deselect();
  TargetTerritory->Select();

  return true;
}

void ATerritory::HandleMouseEnter(UPrimitiveComponent *TouchedComponent) {
  if (!bIsSelected && MeshComponent) {
    MeshComponent->SetRenderCustomDepth(true);
  }
}

void ATerritory::HandleMouseLeave(UPrimitiveComponent *TouchedComponent) {
  if (!bIsSelected && MeshComponent) {
    MeshComponent->SetRenderCustomDepth(false);
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

void ATerritory::RefreshAppearance() {
  UpdateTerritoryColor();
  UpdateLabel();

  if (CapitalMesh) {
    CapitalMesh->SetVisibility(bIsCapital);
    CapitalMesh->SetHiddenInGame(!bIsCapital);
  }
}

void ATerritory::OnRep_OwningPlayer() { RefreshAppearance(); }

void ATerritory::OnRep_IsCapital() { RefreshAppearance(); }

void ATerritory::OnRep_ArmyStrength() { UpdateLabel(); }

void ATerritory::UpdateTerritoryColor() {
  if (DynamicMaterial) {
    FLinearColor NewColor = DefaultColor;
    if (OwningPlayer) {
      NewColor = GetFactionColor(OwningPlayer->Faction);
    }
    DynamicMaterial->SetVectorParameterValue(FName("Color"), NewColor);
    DefaultColor = NewColor;
  }
}

void ATerritory::UpdateLabel() {
  if (!LabelComponent) {
    return;
  }

  const FString OwnerName = OwningPlayer ? OwningPlayer->DisplayName : TEXT("Neutral");
  const FString Text = FString::Printf(TEXT("%s\nOwner: %s\nArmy: %d"),
                                      *TerritoryName, *OwnerName,
                                      ArmyStrength);
  LabelComponent->SetText(FText::FromString(Text));
}
