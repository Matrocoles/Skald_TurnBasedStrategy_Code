#include "WorldMap.h"
#include "Engine/World.h"
#include "Territory.h"

// Table describing each territory that should exist in the world. This
// mirrors the data supplied by design and is used to spawn the territories at
// runtime.
const TArray<FTerritorySpawnData> AWorldMap::DefaultTerritories = {
    FTerritorySpawnData(0, TEXT("Howling Veil"), true),
    FTerritorySpawnData(1, TEXT("Thond")),
    FTerritorySpawnData(2, TEXT("Elmyre")),
    FTerritorySpawnData(3, TEXT("Skarlstorm")),
    FTerritorySpawnData(4, TEXT("Direford")),
    FTerritorySpawnData(5, TEXT("Grimrest")),
    FTerritorySpawnData(6, TEXT("Lakehold")),
    FTerritorySpawnData(7, TEXT("Argoth")),
    FTerritorySpawnData(8, TEXT("Chala"), false, 1),
    FTerritorySpawnData(9, TEXT("Rylan"), false, 1),
    FTerritorySpawnData(10, TEXT("Uris"), false, 1),
    FTerritorySpawnData(11, TEXT("Achre"), true, 1),
    FTerritorySpawnData(12, TEXT("Erif"), false, 2),
    FTerritorySpawnData(13, TEXT("Nevar"), false, 1),
    FTerritorySpawnData(14, TEXT("Frayton")),
    FTerritorySpawnData(15, TEXT("Past Fields"), false, 1),
    FTerritorySpawnData(16, TEXT("Spring Isle"), false, 1),
    FTerritorySpawnData(17, TEXT("Sunder Isle"), false, 2),
    FTerritorySpawnData(18, TEXT("Sugiria"), true, 2),
    FTerritorySpawnData(19, TEXT("Blindshade"), false, 2),
    FTerritorySpawnData(20, TEXT("Whimswallow"), false, 2),
    FTerritorySpawnData(21, TEXT("Forgotten Coast"), false, 2),
    FTerritorySpawnData(22, TEXT("Oria"), false, 2),
    FTerritorySpawnData(23, TEXT("Brell"), false, 3),
    FTerritorySpawnData(24, TEXT("Revel"), true, 3),
    FTerritorySpawnData(25, TEXT("Velaria"), false, 3),
    FTerritorySpawnData(26, TEXT("Essivar"), false, 3),
    FTerritorySpawnData(27, TEXT("Caldemire"), false, 3),
    FTerritorySpawnData(28, TEXT("HazelHallow"), false, 4),
    FTerritorySpawnData(29, TEXT("Sirholde"), false, 4),
    FTerritorySpawnData(30, TEXT("Styr"), false, 4),
    FTerritorySpawnData(31, TEXT("Dawnmere"), true, 4),
    FTerritorySpawnData(32, TEXT("Killbrooke"), false, 4),
    FTerritorySpawnData(33, TEXT("Broken Plains"), false, 5),
    FTerritorySpawnData(34, TEXT("Everlands"), false, 5),
    FTerritorySpawnData(35, TEXT("Vigilmoore"), false, 5),
    FTerritorySpawnData(36, TEXT("Vulkrum"), false, 5),
    FTerritorySpawnData(37, TEXT("Timber Rock"), false, 5),
    FTerritorySpawnData(38, TEXT("Omenwhick"), false, 5),
    FTerritorySpawnData(39, TEXT("Armens Grasp"), false, 5),
    FTerritorySpawnData(40, TEXT("Volkridge"), true, 5),
    FTerritorySpawnData(41, TEXT("Bakas"), false, 5),
    FTerritorySpawnData(42, TEXT("Kesis"), false, 5)};

AWorldMap::AWorldMap() {
  PrimaryActorTick.bCanEverTick = false;
  SelectedTerritory = nullptr;
}

void AWorldMap::BeginPlay() {
  Super::BeginPlay();

  if (!TerritoryClass) {
    return;
  }

  // Spawn each predefined territory at a random location inside the spawn area.
  for (const FTerritorySpawnData &Data : DefaultTerritories) {
    const float X = FMath::FRandRange(SpawnAreaMin.X, SpawnAreaMax.X);
    const float Y = FMath::FRandRange(SpawnAreaMin.Y, SpawnAreaMax.Y);
    const FVector SpawnLocation = GetActorLocation() + FVector(X, Y, 0.f);

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
}

void AWorldMap::RegisterTerritory(ATerritory *Territory) {
  if (Territory && !Territories.Contains(Territory)) {
    Territories.Add(Territory);
    Territory->OnTerritorySelected.AddDynamic(this,
                                              &AWorldMap::SelectTerritory);
  }
}

ATerritory *AWorldMap::GetTerritoryById(int32 TerritoryId) const {
  if (ATerritory **Found = Territories.FindByPredicate(
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
}

bool AWorldMap::MoveBetween(ATerritory *From, ATerritory *To) {
  if (!From || !To) {
    return false;
  }

  return From->MoveTo(To);
}
