#include "Territory.h"
#include "Components/DecalComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "SkaldTypes.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Skald_GameInstance.h"
#include "UObject/ConstructorHelpers.h"
#include "WorldMap.h"

namespace {
FLinearColor ResolveFactionColor(const UObject *WorldContext,
                                 ESkaldFaction Faction) {
  if (const UWorld *World = WorldContext ? WorldContext->GetWorld() : nullptr) {
    if (const USkaldGameInstance *GI =
            World->GetGameInstance<USkaldGameInstance>()) {
      return GI->GetFactionColor(Faction);
    }
  }

  return USkaldGameInstance::GetDefaultFactionColor(Faction);
}
} // namespace

ATerritory::ATerritory() {
  PrimaryActorTick.bCanEverTick = false;
  bReplicates = true;
  MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
  MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  MeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
  // If you are using Visibility for clicks:
  MeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
  // If you switched to a custom click channel (ECC_GameTraceChannel1), use this
  // instead:
  // MeshComponent->SetCollisionResponseToChannel(ECC_GameTraceChannel1,
  // ECR_Block);
  RootComponent = MeshComponent;

  // Provide basic visuals so the world map can function even if assets
  // are missing in the editor. This avoids runtime errors about missing
  // meshes or materials when spawning territories.
  static UStaticMesh *DefaultMesh = LoadObject<UStaticMesh>(
      nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
  if (DefaultMesh) {
    MeshComponent->SetStaticMesh(DefaultMesh);
  }
  static UMaterialInterface *DefaultMat = LoadObject<UMaterialInterface>(
      nullptr,
      TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
  if (DefaultMat) {
    MeshComponent->SetMaterial(0, DefaultMat);
  }

  LabelComponent = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
  LabelComponent->SetupAttachment(RootComponent);
  LabelComponent->SetHorizontalAlignment(EHTA_Center);
  LabelComponent->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
  LabelComponent->SetText(FText::GetEmpty());

  SelectionDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("SelectionDecal"));
  if (SelectionDecal) {
    SelectionDecal->SetupAttachment(RootComponent);
    SelectionDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
    SelectionDecal->DecalSize = SelectionDecalSize;
    SelectionDecal->SetRelativeLocation(
        FVector(0.f, 0.f, SelectionDecalVerticalOffset));
    SelectionDecal->SetVisibility(false);
    SelectionDecal->SetHiddenInGame(true);
    SelectionDecal->SetCanEverAffectNavigation(false);
  }

  SiegeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SiegeMesh"));
  if (SiegeMesh) {
    SiegeMesh->SetupAttachment(RootComponent);
    SiegeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SiegeMesh->SetGenerateOverlapEvents(false);
    SiegeMesh->SetCastShadow(false);
    SiegeMesh->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
    SiegeMesh->SetRelativeScale3D(FVector(0.35f));
    static UStaticMesh *DefaultSiegeMesh = LoadObject<UStaticMesh>(
        nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
    if (DefaultSiegeMesh) {
      SiegeMesh->SetStaticMesh(DefaultSiegeMesh);
    }
    SiegeMesh->SetVisibility(false);
    SiegeMesh->SetHiddenInGame(true);
  }

  OwningPlayer = nullptr;
  GoldYield = 0;
  TerritoryID = 0;
  TerritoryName = TEXT("");
  bIsCapital = false;
  ContinentID = 0;
  ArmyUnits = 0;
}

void ATerritory::OnConstruction(const FTransform &Transform) {
  Super::OnConstruction(Transform);

  UpdateSelectionDecalTransform();
  ApplySelectionDecalMaterial();
}

void ATerritory::BeginPlay() {
  Super::BeginPlay();

  ApplySelectionDecalMaterial();

  if (!CapitalMesh) {
    CapitalMesh = NewObject<UStaticMeshComponent>(this, TEXT("CapitalMesh"));
    if (CapitalMesh) {
      CapitalMesh->SetupAttachment(RootComponent);
      if (CapitalMeshAsset) {
        CapitalMesh->SetStaticMesh(CapitalMeshAsset);
      }
      CapitalMesh->SetRelativeScale3D(FVector(2.f));
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

  UpdateSiegeAppearance();

  // Ensure this territory is registered with the world map. When territories
  // are placed manually in a level they may not have been added during map
  // initialization, so register here if needed.
  if (AWorldMap *WorldMap = Cast<AWorldMap>(
          UGameplayStatics::GetActorOfClass(this, AWorldMap::StaticClass()))) {
    if (!WorldMap->Territories.Contains(this)) {
      WorldMap->RegisterTerritory(this);
    }
  }
}

void ATerritory::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  DOREPLIFETIME(ATerritory, OwningPlayer);
  DOREPLIFETIME(ATerritory, GoldYield);
  DOREPLIFETIME(ATerritory, TerritoryID);
  DOREPLIFETIME(ATerritory, TerritoryName);
  DOREPLIFETIME(ATerritory, bIsCapital);
  DOREPLIFETIME(ATerritory, ContinentID);
  DOREPLIFETIME(ATerritory, AdjacentTerritories);
  DOREPLIFETIME(ATerritory, ArmyUnits);
  DOREPLIFETIME(ATerritory, BuiltSiegeID);
  DOREPLIFETIME(ATerritory, bHasTreasure);
}

void ATerritory::OnRep_BuiltSiegeID() { UpdateSiegeAppearance(); }

void ATerritory::Select(int32 SelectingPlayerId) {
  const bool bShouldShow = ShouldShowSelectionVisuals(SelectingPlayerId);
  LastSelectingPlayerId = SelectingPlayerId;

  if (!bIsSelected && bShouldShow && DynamicMaterial) {
    // Remember the existing color so it can be restored on deselect
    DynamicMaterial->GetVectorParameterValue(FName("Color"), DefaultColor);
  }

  bIsSelected = true;
  UpdateSelectionVisuals(bShouldShow);
}

void ATerritory::Deselect() {
  bIsSelected = false;
  LastSelectingPlayerId = INDEX_NONE;
  UpdateSelectionVisuals(false);
  if (AWorldMap *Map = Cast<AWorldMap>(GetOwner())) {
    if (Map->SelectedTerritory == this) {
      Map->SelectedTerritory = nullptr;
    }
  }
}

void ATerritory::EndPlay(const EEndPlayReason::Type Reason) {
  if (AWorldMap *Map = Cast<AWorldMap>(GetOwner())) {
    if (Map->SelectedTerritory == this) {
      Map->SelectTerritory(nullptr, false);
    }
  }
  Super::EndPlay(Reason);
}

bool ATerritory::IsAdjacentTo(const ATerritory *Other) const {
  return AdjacentTerritories.Contains(Other);
}

void ATerritory::UpdateSiegeAppearance() {
  if (!SiegeMesh) {
    return;
  }

  const bool bHasSiege = BuiltSiegeID != 0;
  SiegeMesh->SetVisibility(bHasSiege);
  SiegeMesh->SetHiddenInGame(!bHasSiege);
}

bool ATerritory::MoveTo(ATerritory *TargetTerritory, int32 Troops) {
  if (!TargetTerritory || Troops <= 0 || Troops >= ArmyUnits) {
    return false;
  }

  if (!IsAdjacentTo(TargetTerritory) ||
      TargetTerritory->OwningPlayer != OwningPlayer) {
    return false;
  }

  ArmyUnits -= Troops;
  TargetTerritory->ArmyUnits += Troops;

  RefreshAppearance();
  TargetTerritory->RefreshAppearance();

  if (AWorldMap *Map = Cast<AWorldMap>(GetOwner())) {
    Map->SelectTerritory(TargetTerritory);
  } else {
    Deselect();
    TargetTerritory->Select();
  }

  return true;
}

void ATerritory::HandleMouseEnter(UPrimitiveComponent *TouchedComponent) {
  if (AWorldMap *Map = Cast<AWorldMap>(GetOwner())) {
    if (!Map->IsWorldActive()) {
      return;
    }
  }

  if (!IsSelectionVisibleToLocalPlayer() && MeshComponent) {
    MeshComponent->SetRenderCustomDepth(true);
  }
}

void ATerritory::HandleMouseLeave(UPrimitiveComponent *TouchedComponent) {
  if (AWorldMap *Map = Cast<AWorldMap>(GetOwner())) {
    if (!Map->IsWorldActive()) {
      return;
    }
  }

  if (!IsSelectionVisibleToLocalPlayer() && MeshComponent) {
    MeshComponent->SetRenderCustomDepth(false);
  }
}

void ATerritory::HandleClicked(UPrimitiveComponent *TouchedComponent,
                               FKey ButtonPressed) {
  if (AWorldMap *Map = Cast<AWorldMap>(GetOwner())) {
    if (!Map->IsWorldActive()) {
      return;
    }
  }

  UE_LOG(LogSkald, Log, TEXT("Territory %d clicked; currently %s"), TerritoryID,
         bIsSelected ? TEXT("selected") : TEXT("not selected"));

  if (ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(
          UGameplayStatics::GetPlayerController(this, 0))) {
    const bool bDeselected = bIsSelected;
    PC->ServerSelectTerritory(bDeselected ? -1 : TerritoryID);
  }
}

void ATerritory::SetSelectionDecalAdditionalHeightOffset(float AdditionalOffset) {
  SelectionDecalAdditionalHeightOffset = AdditionalOffset;
  UpdateSelectionDecalTransform();
}

float ATerritory::GetSelectionDecalEffectiveHeightOffset() const {
  return SelectionDecalVerticalOffset + SelectionDecalAdditionalHeightOffset;
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

void ATerritory::OnRep_ArmyUnits() { UpdateLabel(); }

void ATerritory::UpdateTerritoryColor() {
  if (!DynamicMaterial) {
    return;
  }

  FLinearColor NewColor = DefaultColor;
  if (IsValid(OwningPlayer)) {
    NewColor = ResolveFactionColor(this, OwningPlayer->Faction);
  }

  DynamicMaterial->SetVectorParameterValue(FName("Color"), NewColor);
  DefaultColor = NewColor;
}

void ATerritory::UpdateLabel() {
  if (!LabelComponent) {
    return;
  }

  const ASkaldPlayerState *OwnerPS = IsValid(OwningPlayer) ? OwningPlayer : nullptr;
  const FString OwnerName = OwnerPS
                                ? OwnerPS->GetResolvedPlayerName(
                                      TEXT("Territory::UpdateLabel"))
                                : TEXT("Neutral");
  const FString Text = FString::Printf(TEXT("%s\nOwner: %s\nUnits: %d"),
                                       *TerritoryName, *OwnerName, ArmyUnits);
  LabelComponent->SetText(FText::FromString(Text));
}

void ATerritory::UpdateSelectionDecalTransform() {
  if (!SelectionDecal) {
    return;
  }

  SelectionDecal->DecalSize = SelectionDecalSize;
  SelectionDecal->SetRelativeLocation(
      FVector(0.f, 0.f, GetSelectionDecalEffectiveHeightOffset()));
}

void ATerritory::ApplySelectionDecalMaterial() {
  if (!SelectionDecal) {
    return;
  }

  if (SelectionDecalMaterial) {
    SelectionDecal->SetDecalMaterial(SelectionDecalMaterial);
  }
}

void ATerritory::SetSelectionDecalVisible(bool bVisible) {
  if (!SelectionDecal) {
    return;
  }

  SelectionDecal->SetVisibility(bVisible);
  SelectionDecal->SetHiddenInGame(!bVisible);
}

void ATerritory::UpdateSelectionVisuals(bool bVisible) {
  if (MeshComponent) {
    MeshComponent->SetRenderCustomDepth(bVisible);
  }

  if (DynamicMaterial) {
    if (bVisible) {
      DynamicMaterial->SetVectorParameterValue(FName("Color"),
                                               FLinearColor::White);
    } else {
      DynamicMaterial->SetVectorParameterValue(FName("Color"), DefaultColor);
    }
  }

  SetSelectionDecalVisible(bVisible);
}

bool ATerritory::ShouldShowSelectionVisuals(int32 SelectingPlayerId) const {
  if (SelectingPlayerId == INDEX_NONE) {
    return true;
  }

  const UWorld *World = GetWorld();
  if (!World) {
    return false;
  }

  for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It;
       ++It) {
    const APlayerController *PC = It->Get();
    if (!PC || !PC->IsLocalController()) {
      continue;
    }

    const ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PC->PlayerState);
    if (!PS) {
      continue;
    }

    if (PS->GetPlayerId() == SelectingPlayerId) {
      return true;
    }
  }

  return false;
}

bool ATerritory::IsSelectionVisibleToLocalPlayer() const {
  if (!bIsSelected) {
    return false;
  }

  return ShouldShowSelectionVisuals(LastSelectingPlayerId);
}

USoundBase *ATerritory::GetSelectionSound() const { return SelectionSound; }

float ATerritory::GetSelectionSoundVolumeMultiplier() const {
  return FMath::Max(0.f, SelectionSoundVolumeMultiplier);
}
