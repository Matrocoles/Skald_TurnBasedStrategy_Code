#include "WorldMap.h"
#include "Engine/World.h"
#include "Territory.h"

// Table describing each territory that should exist in the world. This
// mirrors the data supplied by design and is used to spawn the territories at
// runtime.
const TArray<FTerritorySpawnData> AWorldMap::DefaultTerritories = {
    FTerritorySpawnData(0, TEXT("Howling Veil"), true, 0, FVector(-600.0f, -600.0f, 0.f)),
    FTerritorySpawnData(1, TEXT("Thond"), false, 0, FVector(-400.0f, -600.0f, 0.f)),
    FTerritorySpawnData(2, TEXT("Elmyre"), false, 0, FVector(-200.0f, -600.0f, 0.f)),
    FTerritorySpawnData(3, TEXT("Skarlstorm"), false, 0, FVector(0.0f, -600.0f, 0.f)),
    FTerritorySpawnData(4, TEXT("Direford"), false, 0, FVector(200.0f, -600.0f, 0.f)),
    FTerritorySpawnData(5, TEXT("Grimrest"), false, 0, FVector(400.0f, -600.0f, 0.f)),
    FTerritorySpawnData(6, TEXT("Lakehold"), false, 0, FVector(600.0f, -600.0f, 0.f)),
    FTerritorySpawnData(7, TEXT("Argoth"), false, 0, FVector(-600.0f, -400.0f, 0.f)),
    FTerritorySpawnData(8, TEXT("Chala"), false, 1, FVector(-400.0f, -400.0f, 0.f)),
    FTerritorySpawnData(9, TEXT("Rylan"), false, 1, FVector(-200.0f, -400.0f, 0.f)),
    FTerritorySpawnData(10, TEXT("Uris"), false, 1, FVector(0.0f, -400.0f, 0.f)),
    FTerritorySpawnData(11, TEXT("Achre"), true, 1, FVector(200.0f, -400.0f, 0.f)),
    FTerritorySpawnData(12, TEXT("Erif"), false, 2, FVector(400.0f, -400.0f, 0.f)),
    FTerritorySpawnData(13, TEXT("Nevar"), false, 1, FVector(600.0f, -400.0f, 0.f)),
    FTerritorySpawnData(14, TEXT("Frayton"), false, 0, FVector(-600.0f, -200.0f, 0.f)),
    FTerritorySpawnData(15, TEXT("Past Fields"), false, 1, FVector(-400.0f, -200.0f, 0.f)),
    FTerritorySpawnData(16, TEXT("Spring Isle"), false, 1, FVector(-200.0f, -200.0f, 0.f)),
    FTerritorySpawnData(17, TEXT("Sunder Isle"), false, 2, FVector(0.0f, -200.0f, 0.f)),
    FTerritorySpawnData(18, TEXT("Sugiria"), true, 2, FVector(200.0f, -200.0f, 0.f)),
    FTerritorySpawnData(19, TEXT("Blindshade"), false, 2, FVector(400.0f, -200.0f, 0.f)),
    FTerritorySpawnData(20, TEXT("Whimswallow"), false, 2, FVector(600.0f, -200.0f, 0.f)),
    FTerritorySpawnData(21, TEXT("Forgotten Coast"), false, 2, FVector(-600.0f, 0.0f, 0.f)),
    FTerritorySpawnData(22, TEXT("Oria"), false, 2, FVector(-400.0f, 0.0f, 0.f)),
    FTerritorySpawnData(23, TEXT("Brell"), false, 3, FVector(-200.0f, 0.0f, 0.f)),
    FTerritorySpawnData(24, TEXT("Revel"), true, 3, FVector(0.0f, 0.0f, 0.f)),
    FTerritorySpawnData(25, TEXT("Velaria"), false, 3, FVector(200.0f, 0.0f, 0.f)),
    FTerritorySpawnData(26, TEXT("Essivar"), false, 3, FVector(400.0f, 0.0f, 0.f)),
    FTerritorySpawnData(27, TEXT("Caldemire"), false, 3, FVector(600.0f, 0.0f, 0.f)),
    FTerritorySpawnData(28, TEXT("HazelHallow"), false, 4, FVector(-600.0f, 200.0f, 0.f)),
    FTerritorySpawnData(29, TEXT("Sirholde"), false, 4, FVector(-400.0f, 200.0f, 0.f)),
    FTerritorySpawnData(30, TEXT("Styr"), false, 4, FVector(-200.0f, 200.0f, 0.f)),
    FTerritorySpawnData(31, TEXT("Dawnmere"), true, 4, FVector(0.0f, 200.0f, 0.f)),
    FTerritorySpawnData(32, TEXT("Killbrooke"), false, 4, FVector(200.0f, 200.0f, 0.f)),
    FTerritorySpawnData(33, TEXT("Broken Plains"), false, 5, FVector(400.0f, 200.0f, 0.f)),
    FTerritorySpawnData(34, TEXT("Everlands"), false, 5, FVector(600.0f, 200.0f, 0.f)),
    FTerritorySpawnData(35, TEXT("Vigilmoore"), false, 5, FVector(-600.0f, 400.0f, 0.f)),
    FTerritorySpawnData(36, TEXT("Vulkrum"), false, 5, FVector(-400.0f, 400.0f, 0.f)),
    FTerritorySpawnData(37, TEXT("Timber Rock"), false, 5, FVector(-200.0f, 400.0f, 0.f)),
    FTerritorySpawnData(38, TEXT("Omenwhick"), false, 5, FVector(0.0f, 400.0f, 0.f)),
    FTerritorySpawnData(39, TEXT("Armens Grasp"), false, 5, FVector(200.0f, 400.0f, 0.f)),
    FTerritorySpawnData(40, TEXT("Volkridge"), true, 5, FVector(400.0f, 400.0f, 0.f)),
    FTerritorySpawnData(41, TEXT("Bakas"), false, 5, FVector(600.0f, 400.0f, 0.f)),
    FTerritorySpawnData(42, TEXT("Kesis"), false, 5, FVector(-600.0f, 600.0f, 0.f))};

AWorldMap::AWorldMap() {
  PrimaryActorTick.bCanEverTick = false;
  SelectedTerritory = nullptr;
}

void AWorldMap::BeginPlay() {
  Super::BeginPlay();

  if (!TerritoryClass) {
    return;
  }

  // Spawn each predefined territory at its designated location.
  for (const FTerritorySpawnData &Data : DefaultTerritories) {
    const FVector SpawnLocation = GetActorLocation() + Data.Location;

    FActorSpawnParameters Params;
    Params.Owner = this;
    ATerritory *Territory = GetWorld()->SpawnActor<ATerritory>(
        TerritoryClass, SpawnLocation, FRotator::ZeroRotator, Params);
    if (Territory) {
      Territory->TerritoryID = Data.TerritoryID;
      Territory->TerritoryName = Data.TerritoryName;
      Territory->bIsCapital = Data.bIsCapital;
      Territory->ContinentID = Data.ContinentID;
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
        Territories[i]->AdjacentTerritories.Add(Territories[j]);
        Territories[j]->AdjacentTerritories.Add(Territories[i]);
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
  if (ATerritory * const* Found = Territories.FindByPredicate(
          [TerritoryId](ATerritory *Territory) {
            return Territory && Territory->TerritoryID == TerritoryId;
          })) {
    return *Found;
  }
  return nullptr;
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

bool AWorldMap::MoveBetween(ATerritory *From, ATerritory *To) {
  if (!From || !To) {
    return false;
  }

  return From->MoveTo(To);
}
