#include "WorldMap.h"
#include "Engine/World.h"
#include "Territory.h"

AWorldMap::AWorldMap() {
  PrimaryActorTick.bCanEverTick = false;
  SelectedTerritory = nullptr;
}

void AWorldMap::BeginPlay() {
  Super::BeginPlay();

  if (!TerritoryClass || !TerritoryTable) {
    return;
  }

  // Spawn territories defined in the data table.
  TArray<FTerritorySpawnData *> Rows;
  TerritoryTable->GetAllRows(TEXT("TerritoryTable"), Rows);
  for (const FTerritorySpawnData *Data : Rows) {
    if (!Data) {
      continue;
    }
    const FVector SpawnLocation = GetActorLocation() + Data->Location;

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
    }
  }

  // Establish adjacency based on territory positions so that neighboring
  // territories share borders.
  for (int32 i = 0; i < Territories.Num(); ++i) {
    for (int32 j = i + 1; j < Territories.Num(); ++j) {
      if (FVector::Dist2D(Territories[i]->GetActorLocation(),
                          Territories[j]->GetActorLocation()) <=
          AdjacencyDistance) {
        Territories[i]->AdjacentTerritories.Add(Territories[j].Get());
        Territories[j]->AdjacentTerritories.Add(Territories[i].Get());
      }
    }
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

bool AWorldMap::MoveBetween(ATerritory *From, ATerritory *To, int32 Troops) {
  if (!From || !To) {
    return false;
  }

  if (From->OwningPlayer != To->OwningPlayer) {
    return false;
  }

  if (!From->IsAdjacentTo(To)) {
    return false;
  }

  if (Troops <= 0 || Troops >= From->ArmyStrength) {
    return false;
  }

  if (!From->MoveTo(To, Troops)) {
    return false;
  }

  From->RefreshAppearance();
  To->RefreshAppearance();

  From->ForceNetUpdate();
  To->ForceNetUpdate();

  return true;
}
