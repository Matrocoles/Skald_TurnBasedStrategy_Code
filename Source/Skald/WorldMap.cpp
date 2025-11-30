#include "WorldMap.h"
#include "Algo/RandomShuffle.h"
#include "Algo/Reverse.h"
#include "Components/ActorComponent.h"
#include "Components/AudioComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "Containers/Set.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "Skald_GameMode.h"
#include "Skald_PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Math/NumericLimits.h"
#include "Math/UnrealMathUtility.h"
#include "Templates/Function.h"
#include "Territory.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include <cfloat>

// Constructor sets default properties but leaves TerritoryTable unassigned so
// designers must configure it manually.
AWorldMap::AWorldMap() {
  PrimaryActorTick.bCanEverTick = false;
  bReplicates = true;
  SelectedTerritory = nullptr;
  TerritoryClass = ATerritory::StaticClass();
}

void AWorldMap::BeginPlay() {
  Super::BeginPlay();

  // When territories are replicated (client-side), rebuild the local cache so
  // lookups like GetTerritoryById work correctly for HUD interactions.
  if (Territories.Num() == 0) {
    for (TActorIterator<ATerritory> It(GetWorld()); It; ++It) {
      ATerritory *Territory = *It;
      if (IsValid(Territory) && Territory->GetOwner() == this) {
        RegisterTerritory(Territory);
      }
    }
  }

  for (ATerritory *Territory : Territories) {
    if (IsValid(Territory)) {
      Territory->SetSelectionDecalAdditionalHeightOffset(
          TerritorySelectionDecalHeightOffset);
    }
  }
}

void AWorldMap::SetWorldActive(bool bShouldBeActive) {
  if (bIsWorldActive == bShouldBeActive) {
    return;
  }

  bIsWorldActive = bShouldBeActive;

  SetActorHiddenInGame(!bIsWorldActive);
  SetActorTickEnabled(bIsWorldActive);
  SetActorEnableCollision(bIsWorldActive);

  auto ProcessActorComponents =
      [this](AActor *Actor) -> void {
    if (!Actor) {
      return;
    }

    TInlineComponentArray<UPrimitiveComponent *> PrimitiveComponents(Actor);
    for (UPrimitiveComponent *Primitive : PrimitiveComponents) {
      if (!Primitive) {
        continue;
      }

      Primitive->SetHiddenInGame(!bIsWorldActive);
      Primitive->SetVisibility(bIsWorldActive, true);
      Primitive->SetComponentTickEnabled(bIsWorldActive);

      if (!bIsWorldActive) {
        CachedCollisionStates.FindOrAdd(Primitive) =
            Primitive->GetCollisionEnabled();
        Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
      } else {
        if (TEnumAsByte<ECollisionEnabled::Type> *CachedState =
                CachedCollisionStates.Find(Primitive)) {
          Primitive->SetCollisionEnabled(*CachedState);
          CachedCollisionStates.Remove(Primitive);
        }
      }
    }

    TInlineComponentArray<UAudioComponent *> AudioComponents(Actor);
    for (UAudioComponent *Audio : AudioComponents) {
      if (!Audio) {
        continue;
      }

      if (!bIsWorldActive) {
        const bool bWasPlaying = Audio->IsPlaying();
        CachedAudioPlaybackState.FindOrAdd(Audio) = bWasPlaying;
        if (bWasPlaying) {
          Audio->SetPaused(true);
        }
      } else {
        bool bShouldResume = false;
        if (bool *CachedValue = CachedAudioPlaybackState.Find(Audio)) {
          bShouldResume = *CachedValue;
          CachedAudioPlaybackState.Remove(Audio);
        }

        if (bShouldResume) {
          Audio->SetPaused(false);
        }
      }
    }
  };

  ProcessActorComponents(this);

  for (ATerritory *Territory : Territories) {
    if (!IsValid(Territory)) {
      continue;
    }

    Territory->SetActorHiddenInGame(!bIsWorldActive);
    Territory->SetActorTickEnabled(bIsWorldActive);
    Territory->SetActorEnableCollision(bIsWorldActive);

    ProcessActorComponents(Territory);
  }

  for (auto It = CachedCollisionStates.CreateIterator(); It; ++It) {
    if (!It.Key().IsValid()) {
      It.RemoveCurrent();
    }
  }

  for (auto It = CachedAudioPlaybackState.CreateIterator(); It; ++It) {
    if (!It.Key().IsValid()) {
      It.RemoveCurrent();
    }
  }
}

bool AWorldMap::GenerateTerritoriesFromTable() {
  if (!TerritoryClass) {
    UE_LOG(LogSkald, Error, TEXT("WorldMap %s missing TerritoryClass"),
           *GetName());
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          FString::Printf(TEXT("WorldMap %s has no TerritoryClass"),
                          *GetName()));
    }
    return false;
  }

  if (!TerritoryTable) {
    UE_LOG(LogSkald, Error, TEXT("WorldMap %s missing TerritoryTable"),
           *GetName());
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          FString::Printf(
              TEXT("WorldMap %s missing TerritoryTable."
                   " Expected /Game/DataTables/TerritoriesDataTable"),
              *GetName()));
    }
    return false;
  }

  Territories.Empty();
  SpawnedLocations.Reset();
  CapitalCandidateIDs.Reset();

  ATerritory *DefaultTerritory = TerritoryClass->GetDefaultObject<ATerritory>();
  UStaticMeshComponent *MeshComp =
      DefaultTerritory
          ? DefaultTerritory->FindComponentByClass<UStaticMeshComponent>()
          : nullptr;
  if (!MeshComp) {
    UE_LOG(LogSkald, Error,
           TEXT("WorldMap %s TerritoryClass %s missing StaticMeshComponent"),
           *GetName(), *TerritoryClass->GetName());
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          FString::Printf(TEXT("%s missing StaticMeshComponent"),
                          *TerritoryClass->GetName()));
    }
    return false;
  }

  if (!MeshComp->GetStaticMesh()) {
    if (UStaticMesh *FallbackMesh = LoadObject<UStaticMesh>(
            nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"))) {
      MeshComp->SetStaticMesh(FallbackMesh);
      UE_LOG(
          LogSkald, Warning,
          TEXT(
              "WorldMap %s TerritoryClass %s missing mesh; using default cube"),
          *GetName(), *TerritoryClass->GetName());
    } else {
      UE_LOG(
          LogSkald, Error,
          TEXT(
              "WorldMap %s TerritoryClass %s missing mesh and fallback failed"),
          *GetName(), *TerritoryClass->GetName());
      if (GEngine) {
        GEngine->AddOnScreenDebugMessage(
            -1, 5.f, FColor::Red,
            FString::Printf(TEXT("%s missing mesh"),
                            *TerritoryClass->GetName()));
      }
      return false;
    }
  }

  if (MeshComp->GetNumMaterials() == 0) {
    if (UMaterialInterface *FallbackMat = LoadObject<UMaterialInterface>(
            nullptr,
            TEXT(
                "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"))) {
      MeshComp->SetMaterial(0, FallbackMat);
      UE_LOG(LogSkald, Warning,
             TEXT("WorldMap %s TerritoryClass %s missing material; using basic "
                  "material"),
             *GetName(), *TerritoryClass->GetName());
    } else {
      UE_LOG(LogSkald, Error,
             TEXT("WorldMap %s TerritoryClass %s missing material and fallback "
                  "failed"),
             *GetName(), *TerritoryClass->GetName());
      if (GEngine) {
        GEngine->AddOnScreenDebugMessage(
            -1, 5.f, FColor::Red,
            FString::Printf(TEXT("%s missing material"),
                            *TerritoryClass->GetName()));
      }
      return false;
    }
  }

  // Spawn territories defined in the data table.
  TArray<FTerritorySpawnData *> Rows;
  TerritoryTable->GetAllRows(TEXT("TerritoryTable"), Rows);
  TMap<int32, ATerritory *> TerritoriesById;
  TMap<int32, const FTerritorySpawnData *> SpawnDataById;
  TerritoriesById.Reserve(Rows.Num());
  SpawnDataById.Reserve(Rows.Num());

  for (const FTerritorySpawnData *Data : Rows) {
    if (!Data) {
      continue;
    }

    FVector LocalSpawnOffset;
    if (Data->bUseDataTableLocation) {
      LocalSpawnOffset = Data->Location;
    } else {
      const float RandX = FMath::FRandRange(SpawnAreaMin.X, SpawnAreaMax.X);
      const float RandY = FMath::FRandRange(SpawnAreaMin.Y, SpawnAreaMax.Y);
      LocalSpawnOffset = FVector(RandX, RandY, 0.f);
    }

    const FVector SpawnLocation = GetActorLocation() + LocalSpawnOffset;

    FActorSpawnParameters Params;
    Params.Owner = this;
    ATerritory *Territory = GetWorld()->SpawnActor<ATerritory>(
        TerritoryClass, SpawnLocation, FRotator::ZeroRotator, Params);
    if (Territory) {
      Territory->TerritoryID = Data->TerritoryID;
      Territory->TerritoryName = Data->TerritoryName;
      Territory->bIsCapital = Data->bIsCapital;
      Territory->ContinentID = Data->ContinentID;
      Territory->AdjacentTerritories.Reset();
      // Ensure the replicated properties (especially TerritoryID) are sent to
      // clients immediately so selections/requests made right after level load
      // use the correct identifiers instead of the default zero value.
      Territory->ForceNetUpdate();
      RegisterTerritory(Territory);

      SpawnedLocations.Add(Data->TerritoryID, Territory->GetActorLocation());
      TerritoriesById.Add(Data->TerritoryID, Territory);
      SpawnDataById.Add(Data->TerritoryID, Data);

      if (Data->bIsCapital) {
        CapitalCandidateIDs.Add(Data->TerritoryID);
      }
    }
  }

  auto AddAdjacency = [](ATerritory *A, ATerritory *B) {
    if (!A || !B) {
      return;
    }
    if (!A->AdjacentTerritories.Contains(B)) {
      A->AdjacentTerritories.Add(B);
    }
  };

  // Apply table-authored adjacency first so designers have deterministic
  // control when requested.
  TSet<int32> MissingAdjacencyIds;
  for (const TPair<int32, ATerritory *> &Pair : TerritoriesById) {
    ATerritory *Territory = Pair.Value;
    const FTerritorySpawnData *const *SpawnDataPtr =
        SpawnDataById.Find(Pair.Key);
    if (!Territory || !SpawnDataPtr || !*SpawnDataPtr) {
      continue;
    }

    const FTerritorySpawnData *SpawnData = *SpawnDataPtr;
    if (!SpawnData->bOverrideAdjacency ||
        SpawnData->AdjacentTerritoryIDs.Num() == 0) {
      continue;
    }

    for (int32 NeighborId : SpawnData->AdjacentTerritoryIDs) {
      if (NeighborId <= 0) {
        continue;
      }

      if (ATerritory *const *NeighborPtr = TerritoriesById.Find(NeighborId)) {
        ATerritory *Neighbor = *NeighborPtr;
        AddAdjacency(Territory, Neighbor);
        AddAdjacency(Neighbor, Territory);
      } else if (!MissingAdjacencyIds.Contains(NeighborId)) {
        MissingAdjacencyIds.Add(NeighborId);
        UE_LOG(LogSkald, Warning,
               TEXT("WorldMap %s adjacency references unknown territory id %d"),
               *GetName(), NeighborId);
      }
    }
  }

  auto HasManualAdjacency =
      [&SpawnDataById](const ATerritory *Territory) -> bool {
    if (!Territory) {
      return false;
    }
    const FTerritorySpawnData *const *SpawnDataPtr =
        SpawnDataById.Find(Territory->TerritoryID);
    return SpawnDataPtr && *SpawnDataPtr && (*SpawnDataPtr)->bOverrideAdjacency;
  };

  // Build adjacency by distance.
  for (int32 i = 0; i < Territories.Num(); ++i) {
    ATerritory *A = Territories[i];
    if (!A) {
      continue;
    }
    const bool bAManual = HasManualAdjacency(A);
    for (int32 j = i + 1; j < Territories.Num(); ++j) {
      ATerritory *B = Territories[j];
      if (!B) {
        continue;
      }
      if (bAManual || HasManualAdjacency(B)) {
        continue;
      }
      const float Dist =
          FVector::Dist(A->GetActorLocation(), B->GetActorLocation());
      if (Dist <= AdjacencyDistance) {
        AddAdjacency(A, B);
        AddAdjacency(B, A);
      }
    }
  }

  // Ensure every territory has at least one neighbor.
  for (ATerritory *Territory : Territories) {
    if (!Territory || Territory->AdjacentTerritories.Num() > 0) {
      continue;
    }
    float BestDist = FLT_MAX;
    ATerritory *Closest = nullptr;
    const FVector Loc = Territory->GetActorLocation();
    for (ATerritory *Other : Territories) {
      if (!Other || Other == Territory) {
        continue;
      }
      const float Dist = FVector::Dist(Loc, Other->GetActorLocation());
      if (Dist < BestDist) {
        BestDist = Dist;
        Closest = Other;
      }
    }
    if (Closest) {
      Territory->AdjacentTerritories.Add(Closest);
      if (!Closest->AdjacentTerritories.Contains(Territory)) {
        Closest->AdjacentTerritories.Add(Territory);
      }
    }
  }

  // Connect separate graph components by linking closest territories using a
  // union-find structure to avoid repeatedly gathering components.
  TMap<ATerritory *, ATerritory *> Parent;
  Parent.Reserve(Territories.Num());
  for (ATerritory *Terr : Territories) {
    if (Terr) {
      Parent.Add(Terr, Terr);
    }
  }

  // Find with path compression.
  TFunction<ATerritory *(ATerritory *)> FindRoot =
      [&](ATerritory *Territory) -> ATerritory * {
    ATerritory **Ptr = Parent.Find(Territory);
    if (!Ptr) {
      return nullptr;
    }
    ATerritory *Root = *Ptr;
    if (Root != Territory) {
      Root = FindRoot(Root);
      Parent[Territory] = Root;
    }
    return Root;
  };

  // Union two sets; return true if merged.
  auto Union = [&](ATerritory *A, ATerritory *B) {
    ATerritory *RootA = FindRoot(A);
    ATerritory *RootB = FindRoot(B);
    if (!RootA || !RootB || RootA == RootB) {
      return false;
    }
    Parent[RootB] = RootA;
    return true;
  };

  int32 ComponentCount = Parent.Num();
  // Merge sets based on existing adjacency.
  for (ATerritory *Terr : Territories) {
    if (!Terr) {
      continue;
    }
    for (ATerritory *Neighbor : Terr->AdjacentTerritories) {
      if (Neighbor && Union(Terr, Neighbor)) {
        --ComponentCount;
      }
    }
  }

  while (ComponentCount > 1) {
    float BestDist = FLT_MAX;
    ATerritory *A = nullptr;
    ATerritory *B = nullptr;
    for (int32 i = 0; i < Territories.Num(); ++i) {
      ATerritory *T1 = Territories[i];
      if (!T1) {
        continue;
      }
      for (int32 j = i + 1; j < Territories.Num(); ++j) {
        ATerritory *T2 = Territories[j];
        if (!T2 || FindRoot(T1) == FindRoot(T2)) {
          continue;
        }
        const float Dist =
            FVector::Dist(T1->GetActorLocation(), T2->GetActorLocation());
        if (Dist < BestDist) {
          BestDist = Dist;
          A = T1;
          B = T2;
        }
      }
    }
    if (!A || !B) {
      break;
    }
    if (!A->AdjacentTerritories.Contains(B)) {
      A->AdjacentTerritories.Add(B);
    }
    if (!B->AdjacentTerritories.Contains(A)) {
      B->AdjacentTerritories.Add(A);
    }
    if (Union(A, B)) {
      --ComponentCount;
    }
  }

  return Territories.Num() > 0;
}

void AWorldMap::RegisterTerritory(ATerritory *Territory) {
  if (Territory && !Territories.Contains(Territory)) {
    if (HasAuthority()) {
      if (Territory->TerritoryID == 0) {
        UE_LOG(LogSkald, Warning,
               TEXT("WorldMap %s registering territory %s with missing ID; clients may see ID 0"),
               *GetName(), *Territory->GetName());
      }
      Territory->ForceNetUpdate();
    }

    Territory->SetOwner(this);
    Territory->SetSelectionDecalAdditionalHeightOffset(
        TerritorySelectionDecalHeightOffset);
    Territories.Add(Territory);
  }
}

bool AWorldMap::IsSelectingPlayerLocal(int32 SelectingPlayerId) const {
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

    if (PS->GetStablePlayerId() == SelectingPlayerId) {
      return true;
    }
  }

  return false;
}

ATerritory *AWorldMap::GetTerritoryById(int32 TerritoryId) const {
  const TObjectPtr<ATerritory> *Found = Territories.FindByPredicate(
      [TerritoryId](const TObjectPtr<ATerritory> &Territory) {
        return Territory && Territory->TerritoryID == TerritoryId;
      });
  return Found ? *Found : nullptr;
}

ATerritory *AWorldMap::GetSelectionForPlayer(int32 PlayerId) const {
  const TWeakObjectPtr<ATerritory> *Found = SelectionByPlayerId.Find(PlayerId);
  if (!Found) {
    return nullptr;
  }

  if (Found->IsValid()) {
    return Found->Get();
  }
  return nullptr;
}

ATerritory *AWorldMap::GetLocalSelection() const {
  const UWorld *World = GetWorld();
  if (!World) {
    return SelectedTerritory;
  }

  for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator();
       It; ++It) {
    const APlayerController *PC = It->Get();
    if (!PC || !PC->IsLocalController()) {
      continue;
    }

    const ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PC->PlayerState);
    if (!PS) {
      continue;
    }

    const int32 StableId = PS->GetStablePlayerId();
    if (StableId == INDEX_NONE) {
      continue;
    }

    const TWeakObjectPtr<ATerritory> *Selection =
        SelectionByPlayerId.Find(StableId);
    if (Selection && Selection->IsValid()) {
      return Selection->Get();
    }

    break;
  }

  return SelectedTerritory;
}

void AWorldMap::SelectTerritory(ATerritory *Territory,
                                bool bPlaySelectionSound,
                                int32 SelectingPlayerId) {
  if (!bIsWorldActive && Territory) {
    UE_LOG(LogSkald, Verbose,
           TEXT("WorldMap %s ignoring selection while inactive"),
           *GetName());
    return;
  }

  const bool bIsGlobalSelection = SelectingPlayerId == INDEX_NONE;
  const bool bAffectsLocalSelection = Territory
                                          ? Territory->ShouldAffectLocalSelection(
                                                SelectingPlayerId)
                                          : (bIsGlobalSelection ||
                                             IsSelectingPlayerLocal(
                                                 SelectingPlayerId));
  if (SelectingPlayerId != INDEX_NONE) {
    if (IsValid(Territory)) {
      SelectionByPlayerId.FindOrAdd(SelectingPlayerId) = Territory;
    } else {
      SelectionByPlayerId.Remove(SelectingPlayerId);
    }
  }

  if (!bAffectsLocalSelection) {
    UE_LOG(LogSkald, VeryVerbose,
           TEXT("WorldMap %s ignoring non-local selection from player %d"),
           *GetName(), SelectingPlayerId);
    return;
  }

  if (Territory == SelectedTerritory &&
      SelectingPlayerId == SelectedByPlayerId) {
    return;
  }

  UE_LOG(LogSkald, Log,
         TEXT("WorldMap %s selecting territory %s (previous %s)"), *GetName(),
         Territory ? *Territory->GetName() : TEXT("None"),
         SelectedTerritory ? *SelectedTerritory->GetName() : TEXT("None"));

  if (bAffectsLocalSelection) {
    if (IsValid(SelectedTerritory)) {
      SelectedTerritory->Deselect();
    }

    SelectedTerritory = IsValid(Territory) ? Territory : nullptr;
    SelectedByPlayerId = SelectedTerritory ? SelectingPlayerId : INDEX_NONE;

    bool bShouldPlaySound = false;
    USoundBase *SoundToPlay = nullptr;
    float VolumeMultiplier = 1.f;
    if (SelectedTerritory) {
      SelectedTerritory->Select(SelectingPlayerId);

      SoundToPlay = SelectedTerritory->GetSelectionSound();
      if (SoundToPlay) {
        VolumeMultiplier =
            SelectedTerritory->GetSelectionSoundVolumeMultiplier();
      } else {
        SoundToPlay = TerritorySelectedSound;
      }

      bShouldPlaySound = bPlaySelectionSound && SoundToPlay &&
                        GetNetMode() != NM_DedicatedServer &&
                        SelectedTerritory->IsSelectionVisibleToLocalPlayer();
    }

    if (bShouldPlaySound) {
      UGameplayStatics::PlaySound2D(this, SoundToPlay, VolumeMultiplier);
    }

    OnTerritorySelected.Broadcast(SelectedTerritory);
  }
}

void AWorldMap::MulticastSelectTerritory_Implementation(int32 TerritoryID,
                                                        int32 SelectingPlayerId) {
  ATerritory *Terr = GetTerritoryById(TerritoryID);
  SelectTerritory(Terr, true, SelectingPlayerId);
}

bool AWorldMap::FindPath(ATerritory *From, ATerritory *To,
                         TArray<ATerritory *> &OutPath) const {
  OutPath.Reset();
  if (!From || !To) {
    return false;
  }

  if (From == To) {
    OutPath.Add(From);
    return true;
  }

  ASkaldPlayerState *StartOwner = From->OwningPlayer;
  TQueue<ATerritory *> Frontier;
  TMap<ATerritory *, ATerritory *> CameFrom;

  Frontier.Enqueue(From);
  CameFrom.Add(From, nullptr);

  while (!Frontier.IsEmpty()) {
    ATerritory *Current = nullptr;
    Frontier.Dequeue(Current);
    if (Current == To) {
      break;
    }

    for (ATerritory *Neighbor : Current->AdjacentTerritories) {
      if (!Neighbor || !IsOwnedBy(Neighbor, StartOwner) ||
          CameFrom.Contains(Neighbor)) {
        continue;
      }
      Frontier.Enqueue(Neighbor);
      CameFrom.Add(Neighbor, Current);
    }
  }

  if (!CameFrom.Contains(To)) {
    return false;
  }

  ATerritory *Step = To;
  while (Step) {
    OutPath.Add(Step);
    Step = CameFrom[Step];
  }
  Algo::Reverse(OutPath);

  return true;
}

bool AWorldMap::MoveBetween(ATerritory *From, ATerritory *To, int32 Troops) {
  if (!From || !To) {
    return false;
  }

  if (!IsOwnedBy(To, From->OwningPlayer)) {
    return false;
  }

  if (Troops <= 0 || Troops >= From->ArmyUnits) {
    return false;
  }

  TArray<ATerritory *> Path;
  if (!FindPath(From, To, Path) || Path.Num() < 2) {
    return false;
  }

  From->ArmyUnits -= Troops;
  To->ArmyUnits += Troops;

  From->RefreshAppearance();
  To->RefreshAppearance();

  SelectTerritory(To);

  if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
    GM->CheckVictoryConditions();
  }

  return true;
}

bool AWorldMap::IsCapitalCandidate(int32 TerritoryId) const {
  return CapitalCandidateIDs.Contains(TerritoryId);
}

bool AWorldMap::AreTerritoriesAdjacent(const ATerritory *A,
                                       const ATerritory *B) const {
  if (!A || !B) {
    return false;
  }
  if (A == B) {
    return false;
  }
  if (A->IsAdjacentTo(B) || B->IsAdjacentTo(A)) {
    return true;
  }
  const float Dist =
      FVector::Dist(A->GetActorLocation(), B->GetActorLocation());
  return Dist <= AdjacencyDistance;
}

bool AWorldMap::IsOwnedBy(const ATerritory *Territory,
                          const ASkaldPlayerState *Player) const {
  if (!Territory || !Player || !IsValid(Territory->OwningPlayer)) {
    return false;
  }

  const int32 TerritoryStableId = Territory->OwningPlayer->GetStablePlayerId();
  const int32 CandidateStableId = Player->GetStablePlayerId();
  if (TerritoryStableId > 0 && CandidateStableId > 0) {
    return TerritoryStableId == CandidateStableId;
  }

  // Fall back to transient player IDs only if no stable identifiers are set
  // yet (e.g., very early in initialization) so ownership still resolves.
  return Territory->OwningPlayer->GetPlayerId() == Player->GetPlayerId();
}

int32 AWorldMap::AutoPlaceUnitsForAI(ASkaldPlayerState *PlayerState) {
  if (!PlayerState || PlayerState->DeployableUnits <= 0) {
    return 0;
  }

  TArray<ATerritory *> OwnedTerritories;
  for (ATerritory *Territory : Territories) {
    if (Territory && Territory->OwningPlayer == PlayerState) {
      OwnedTerritories.Add(Territory);
    }
  }

  if (OwnedTerritories.Num() == 0) {
    UE_LOG(LogSkald, Warning,
           TEXT("AutoPlaceUnitsForAI: PlayerState %d owns no territories"),
           PlayerState->GetStablePlayerId());
    return 0;
  }

  int32 UnitsPlaced = 0;
  int32 Remaining = PlayerState->DeployableUnits;
  int32 Index = 0;
  int32 ConsecutiveSkips = 0;

  const int32 MaxPerTerritory = Skald::ArmyPlacement::DeployPerTerritoryLimit;

  while (Remaining > 0 && OwnedTerritories.Num() > 0) {
    ATerritory *Target = OwnedTerritories[Index % OwnedTerritories.Num()];
    if (!Target) {
      ++Index;
      ++ConsecutiveSkips;
      if (ConsecutiveSkips >= OwnedTerritories.Num()) {
        break;
      }
      continue;
    }

    const int32 TerritoryId = Target->GetTerritoryId();
    const int32 AlreadyPlaced =
        PlayerState->GetArmyPlacementDeploymentForTerritory(TerritoryId);
    if (AlreadyPlaced >= MaxPerTerritory) {
      ++Index;
      ++ConsecutiveSkips;
      if (ConsecutiveSkips >= OwnedTerritories.Num()) {
        break;
      }
      continue;
    }

    ConsecutiveSkips = 0;

    ++Target->ArmyUnits;
    Target->RefreshAppearance();
    ++UnitsPlaced;
    --Remaining;
    PlayerState->AddArmyPlacementDeployment(TerritoryId, 1);
    ++Index;
  }

  PlayerState->DeployableUnits = Remaining;
  return UnitsPlaced;
}

int32 AWorldMap::DistributeUnplacedArmyPlacementUnits(
    ASkaldPlayerState *PlayerState) {
  if (!PlayerState || PlayerState->DeployableUnits <= 0) {
    return 0;
  }

  TArray<ATerritory *> OwnedTerritories;
  OwnedTerritories.Reserve(Territories.Num());
  for (ATerritory *Territory : Territories) {
    if (Territory && Territory->OwningPlayer == PlayerState) {
      OwnedTerritories.Add(Territory);
    }
  }

  if (OwnedTerritories.Num() == 0) {
    UE_LOG(LogSkald, Warning,
           TEXT("DistributeUnplacedArmyPlacementUnits: Player %s has no owned territories"),
           *PlayerState->GetResolvedPlayerName(TEXT("DistributeUnplacedArmyPlacementUnits")));
    return 0;
  }

  TMap<ATerritory *, int32> PendingAssignments;
  PendingAssignments.Reserve(OwnedTerritories.Num());
  TMap<ATerritory *, int32> ProjectedArmyStrength;
  ProjectedArmyStrength.Reserve(OwnedTerritories.Num());

  for (ATerritory *Territory : OwnedTerritories) {
    if (Territory) {
      ProjectedArmyStrength.Add(Territory, Territory->ArmyUnits);
    }
  }

  int32 Remaining = PlayerState->DeployableUnits;
  int32 UnitsPlaced = 0;

  const bool bHasManualDeployments = PlayerState->HasArmyPlacementDeployments();

  if (!bHasManualDeployments) {
    TArray<ATerritory *> Shuffled = OwnedTerritories;
    if (Shuffled.Num() > 1) {
      Algo::RandomShuffle(Shuffled);
    }

    const int32 TerritoryCount = Shuffled.Num();
    const int32 BaseAllocation = TerritoryCount > 0 ? Remaining / TerritoryCount : 0;
    int32 Remainder = TerritoryCount > 0 ? Remaining % TerritoryCount : 0;

    for (int32 Index = 0; Index < Shuffled.Num() && Remaining > 0; ++Index) {
      ATerritory *Territory = Shuffled[Index];
      if (!Territory) {
        continue;
      }

      int32 Allocation = BaseAllocation;
      if (Remainder > 0) {
        ++Allocation;
        --Remainder;
      }

      if (Allocation <= 0) {
        continue;
      }

      PendingAssignments.FindOrAdd(Territory) += Allocation;
      if (int32 *Projected = ProjectedArmyStrength.Find(Territory)) {
        *Projected += Allocation;
      }
      UnitsPlaced += Allocation;
      Remaining -= Allocation;
    }
  } else {
    while (Remaining > 0) {
      int32 WeakestStrength = TNumericLimits<int32>::Max();
      for (const TPair<ATerritory *, int32> &Entry : ProjectedArmyStrength) {
        if (Entry.Key) {
          WeakestStrength = FMath::Min(WeakestStrength, Entry.Value);
        }
      }

      if (WeakestStrength == TNumericLimits<int32>::Max()) {
        break;
      }

      TArray<ATerritory *> Candidates;
      for (const TPair<ATerritory *, int32> &Entry : ProjectedArmyStrength) {
        if (Entry.Key && Entry.Value == WeakestStrength) {
          Candidates.Add(Entry.Key);
        }
      }

      if (Candidates.Num() == 0) {
        break;
      }

      ATerritory *ChosenTerritory =
          Candidates[FMath::RandHelper(FMath::Max(Candidates.Num(), 1))];

      PendingAssignments.FindOrAdd(ChosenTerritory) += 1;
      if (int32 *Projected = ProjectedArmyStrength.Find(ChosenTerritory)) {
        ++(*Projected);
      }

      ++UnitsPlaced;
      --Remaining;
    }
  }

  if (UnitsPlaced <= 0) {
    return 0;
  }

  for (const TPair<ATerritory *, int32> &Assignment : PendingAssignments) {
    ATerritory *Territory = Assignment.Key;
    if (!Territory) {
      continue;
    }

    Territory->ArmyUnits += Assignment.Value;
    Territory->RefreshAppearance();
  }

  Remaining = FMath::Max(Remaining, 0);
  PlayerState->DeployableUnits = Remaining;

  return UnitsPlaced;
}
