#include "WorldMap.h"
#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "Containers/Set.h"
#include <float.h>
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Skald.h"
#include "Skald_GameMode.h"
#include "Territory.h"

AWorldMap::AWorldMap() {
  PrimaryActorTick.bCanEverTick = false;
  SelectedTerritory = nullptr;
  TerritoryClass = ATerritory::StaticClass();
}

void AWorldMap::BeginPlay() {
  Super::BeginPlay();

  if (!TerritoryClass) {
    UE_LOG(LogSkald, Error, TEXT("WorldMap %s missing TerritoryClass"),
           *GetName());
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          FString::Printf(TEXT("WorldMap %s has no TerritoryClass"),
                          *GetName()));
    }
    return;
  }

  if (!TerritoryTable) {
    UE_LOG(LogSkald, Error, TEXT("WorldMap %s missing TerritoryTable"),
           *GetName());
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          FString::Printf(TEXT("WorldMap %s missing TerritoryTable"),
                          *GetName()));
    }

    FActorSpawnParameters Params;
    Params.Owner = this;
    ATerritory *Placeholder = GetWorld()->SpawnActor<ATerritory>(
        TerritoryClass, GetActorLocation(), FRotator::ZeroRotator, Params);
    if (Placeholder) {
      Placeholder->TerritoryID = -1;
      Placeholder->TerritoryName = TEXT("Placeholder Territory");
      RegisterTerritory(Placeholder);
    }

    return;
  }

  ATerritory *DefaultTerritory = TerritoryClass->GetDefaultObject<ATerritory>();
  UStaticMeshComponent *MeshComp =
      DefaultTerritory
          ? DefaultTerritory->FindComponentByClass<UStaticMeshComponent>()
          : nullptr;
  if (!MeshComp || !MeshComp->GetStaticMesh() ||
      MeshComp->GetNumMaterials() == 0) {
    const FString MissingAsset =
        (!MeshComp || !MeshComp->GetStaticMesh()) ? TEXT("mesh") : TEXT("material");
    UE_LOG(LogSkald, Error,
           TEXT("WorldMap %s TerritoryClass %s missing %s"), *GetName(),
           *TerritoryClass->GetName(), *MissingAsset);
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          FString::Printf(TEXT("%s missing %s"),
                          *TerritoryClass->GetName(), *MissingAsset));
    }
    return;
  }

  // Spawn territories defined in the data table at random locations.
  TArray<FTerritorySpawnData *> Rows;
  TerritoryTable->GetAllRows(TEXT("TerritoryTable"), Rows);
  for (const FTerritorySpawnData *Data : Rows) {
    if (!Data) {
      continue;
    }

    const float RandX = FMath::FRandRange(SpawnAreaMin.X, SpawnAreaMax.X);
    const float RandY = FMath::FRandRange(SpawnAreaMin.Y, SpawnAreaMax.Y);
    const FVector SpawnLocation =
        GetActorLocation() + FVector(RandX, RandY, 0.f);

    FActorSpawnParameters Params;
    Params.Owner = this;
    ATerritory *Territory = GetWorld()->SpawnActor<ATerritory>(
        TerritoryClass, SpawnLocation, FRotator::ZeroRotator, Params);
    if (Territory) {
      Territory->TerritoryID = Data->TerritoryID;
      Territory->TerritoryName = Data->TerritoryName;
      Territory->bIsCapital = Data->bIsCapital;
      Territory->ContinentID = Data->ContinentID;
      RegisterTerritory(Territory);

      SpawnedLocations.Add(Data->TerritoryID, SpawnLocation);
    }
  }

  // Build adjacency by distance.
  for (int32 i = 0; i < Territories.Num(); ++i) {
    ATerritory *A = Territories[i];
    if (!A) {
      continue;
    }
    for (int32 j = i + 1; j < Territories.Num(); ++j) {
      ATerritory *B = Territories[j];
      if (!B) {
        continue;
      }
      const float Dist =
          FVector::Dist(A->GetActorLocation(), B->GetActorLocation());
      if (Dist <= AdjacencyDistance) {
        if (!A->AdjacentTerritories.Contains(B)) {
          A->AdjacentTerritories.Add(B);
        }
        if (!B->AdjacentTerritories.Contains(A)) {
          B->AdjacentTerritories.Add(A);
        }
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

  // Connect separate graph components by linking closest territories.
  auto GatherComponents = [&]() {
    TArray<TArray<ATerritory *>> Components;
    TSet<ATerritory *> Visited;
    for (ATerritory *Terr : Territories) {
      if (!Terr || Visited.Contains(Terr)) {
        continue;
      }
      TArray<ATerritory *> Component;
      TQueue<ATerritory *> Queue;
      Queue.Enqueue(Terr);
      Visited.Add(Terr);
      while (!Queue.IsEmpty()) {
        ATerritory *Current = nullptr;
        Queue.Dequeue(Current);
        Component.Add(Current);
        for (ATerritory *Neighbor : Current->AdjacentTerritories) {
          if (Neighbor && !Visited.Contains(Neighbor)) {
            Visited.Add(Neighbor);
            Queue.Enqueue(Neighbor);
          }
        }
      }
      Components.Add(Component);
    }
    return Components;
  };

  TArray<TArray<ATerritory *>> Components = GatherComponents();
  while (Components.Num() > 1) {
    float BestDist = FLT_MAX;
    ATerritory *A = nullptr;
    ATerritory *B = nullptr;
    for (int32 i = 0; i < Components.Num(); ++i) {
      for (int32 j = i + 1; j < Components.Num(); ++j) {
        for (ATerritory *T1 : Components[i]) {
          for (ATerritory *T2 : Components[j]) {
            const float Dist = FVector::Dist(
                T1->GetActorLocation(), T2->GetActorLocation());
            if (Dist < BestDist) {
              BestDist = Dist;
              A = T1;
              B = T2;
            }
          }
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
    Components = GatherComponents();
  }
}

void AWorldMap::RegisterTerritory(ATerritory *Territory) {
  if (Territory && !Territories.Contains(Territory)) {
    Territories.Add(Territory);
    Territory->OnTerritorySelected.AddDynamic(this,
                                              &AWorldMap::SelectTerritory);
  }
}

ATerritory *AWorldMap::GetTerritoryById(int32 TerritoryId) const {
  const TObjectPtr<ATerritory> *Found = Territories.FindByPredicate(
      [TerritoryId](const TObjectPtr<ATerritory> &Territory) {
        return Territory && Territory->TerritoryID == TerritoryId;
      });
  return Found ? *Found : nullptr;
}

void AWorldMap::SelectTerritory(ATerritory *Territory) {
  if (Territory == SelectedTerritory) {
    return;
  }

  if (SelectedTerritory) {
    SelectedTerritory->Deselect();
  }

  SelectedTerritory = Territory;

  OnTerritorySelected.Broadcast(Territory);
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
      if (!Neighbor || Neighbor->OwningPlayer != StartOwner ||
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
    OutPath.Insert(Step, 0);
    Step = CameFrom[Step];
  }

  return true;
}

bool AWorldMap::MoveBetween(ATerritory *From, ATerritory *To, int32 Troops) {
  if (!From || !To) {
    return false;
  }

  if (From->OwningPlayer != To->OwningPlayer) {
    return false;
  }

  if (Troops <= 0 || Troops >= From->ArmyStrength) {
    return false;
  }

  TArray<ATerritory *> Path;
  if (!FindPath(From, To, Path) || Path.Num() < 2) {
    return false;
  }

  for (int32 i = 0; i < Path.Num() - 1; ++i) {
    ATerritory *Curr = Path[i];
    ATerritory *Next = Path[i + 1];
    if (!Curr->MoveTo(Next, Troops)) {
      return false;
    }
    Curr->RefreshAppearance();
    Next->RefreshAppearance();
    Curr->ForceNetUpdate();
    Next->ForceNetUpdate();
  }

  if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
    GM->CheckVictoryConditions();
  }

  return true;
}
