#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "WorldMap.generated.h"

class ATerritory;
class ASkaldPlayerState;
class UPrimitiveComponent;
class UAudioComponent;
class USoundBase;

// Broadcast when a territory is selected on the world map so that interested
// systems (e.g. player controllers) can react.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWorldMapTerritorySelected,
                                            ATerritory *, Territory);

/** Data describing how a territory should be spawned at runtime. */
USTRUCT(BlueprintType)
struct FTerritorySpawnData : public FTableRowBase {
  GENERATED_BODY();

  FTerritorySpawnData()
      : TerritoryID(0), TerritoryName(TEXT("")), bIsCapital(false),
        ContinentID(0), Location(FVector::ZeroVector),
        bUseDataTableLocation(false), bOverrideAdjacency(false),
        AdjacentTerritoryIDs() {}

  FTerritorySpawnData(int32 InID, const FString &InName, bool bCapital = false,
                      int32 InContinent = 0,
                      FVector InLocation = FVector::ZeroVector,
                      const TArray<int32> &InAdjacents = {},
                      bool bUseLocation = false,
                      bool bOverrideAdjacencyIn = false)
      : TerritoryID(InID), TerritoryName(InName), bIsCapital(bCapital),
        ContinentID(InContinent), Location(InLocation),
        bUseDataTableLocation(bUseLocation),
        bOverrideAdjacency(bOverrideAdjacencyIn),
        AdjacentTerritoryIDs(InAdjacents) {}

  UPROPERTY(EditAnywhere, BlueprintReadOnly)
  int32 TerritoryID;

  UPROPERTY(EditAnywhere, BlueprintReadOnly)
  FString TerritoryName;

  UPROPERTY(EditAnywhere, BlueprintReadOnly)
  bool bIsCapital;

  UPROPERTY(EditAnywhere, BlueprintReadOnly)
  int32 ContinentID;

  /** Location for spawning this territory relative to the world map actor. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly)
  FVector Location;

  /** When true, the authored Location will be used instead of a random point. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly)
  bool bUseDataTableLocation;

  /**
   * When true, AdjacentTerritoryIDs is treated as the source of truth and
   * procedural adjacency generation will be skipped for this territory.
   */
  UPROPERTY(EditAnywhere, BlueprintReadOnly)
  bool bOverrideAdjacency;

  /** IDs of territories adjacent to this one. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly)
  TArray<int32> AdjacentTerritoryIDs;
};

/**
 * Actor owning and managing all territories in the map.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API AWorldMap : public AActor {
  GENERATED_BODY()

public:
  AWorldMap();

  virtual void BeginPlay() override;

  /** All territories contained in this world map. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldMap")
  TArray<TObjectPtr<ATerritory>> Territories;

  /** Currently selected territory. */
  UPROPERTY(BlueprintReadOnly, Category = "WorldMap")
  ATerritory *SelectedTerritory;

  /** Event fired whenever SelectTerritory chooses a new territory. */
  UPROPERTY(BlueprintAssignable, Category = "WorldMap")
  FWorldMapTerritorySelected OnTerritorySelected;

  /** Register a territory with the world map. */
  UFUNCTION(BlueprintCallable, Category = "WorldMap")
  void RegisterTerritory(ATerritory *Territory);

  /** Get a territory by its identifier. */
  UFUNCTION(BlueprintCallable, Category = "WorldMap")
  ATerritory *GetTerritoryById(int32 TerritoryId) const;

  /** Sound played when a territory becomes selected on this map. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldMap|Audio")
  USoundBase *TerritorySelectedSound = nullptr;

  /** Handle territory selection. */
  UFUNCTION(BlueprintCallable, Category = "WorldMap")
  void SelectTerritory(ATerritory *Territory, bool bPlaySelectionSound = true);

  UFUNCTION(NetMulticast, Reliable)
  void MulticastSelectTerritory(int32 TerritoryID);

  /** Generate territories from the assigned data table. */
  UFUNCTION(BlueprintCallable, Category = "WorldMap")
  bool GenerateTerritoriesFromTable();

  /** Find a path across friendly territories from one territory to another. */
  UFUNCTION(BlueprintCallable, Category = "WorldMap")
  bool FindPath(ATerritory *From, ATerritory *To,
                TArray<ATerritory *> &OutPath) const;

  /** Move units between territories. */
  UFUNCTION(BlueprintCallable, Category = "WorldMap")
  bool MoveBetween(ATerritory *From, ATerritory *To, int32 Troops);

  /** Toggle whether the overworld should be visible and interactive. */
  UFUNCTION(BlueprintCallable, Category = "WorldMap")
  void SetWorldActive(bool bShouldBeActive);

  /** Returns whether the overworld is currently visible/interactive. */
  UFUNCTION(BlueprintPure, Category = "WorldMap")
  bool IsWorldActive() const { return bIsWorldActive; }

  /** Actor class used when spawning territory instances. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldMap")
  TSubclassOf<ATerritory> TerritoryClass;

  /** Data table defining territories to spawn. */
  UPROPERTY(EditAnywhere, Category = "WorldMap")
  UDataTable *TerritoryTable;

  /** Minimum XY (in local space) for random spawn positions. */
  UPROPERTY(EditAnywhere, Category = "WorldMap")
  FVector2D SpawnAreaMin = FVector2D(-500.f, -500.f);

  /** Maximum XY (in local space) for random spawn positions. */
  UPROPERTY(EditAnywhere, Category = "WorldMap")
  FVector2D SpawnAreaMax = FVector2D(500.f, 500.f);

  /** Maximum distance (in Unreal units) to consider two territories adjacent.
   */
  UPROPERTY(EditAnywhere, Config, Category = "WorldMap")
  float AdjacencyDistance = 5000.f;

  /** Randomly generated spawn locations keyed by territory ID. */
  UPROPERTY(BlueprintReadOnly, Category = "WorldMap")
  TMap<int32, FVector> SpawnedLocations;

  /** Returns true when the supplied territory ID is flagged as a capital in
   *  the data table. */
  bool IsCapitalCandidate(int32 TerritoryId) const;

  /** Check whether two territories are adjacent, falling back to distance. */
  UFUNCTION(BlueprintCallable, Category = "WorldMap")
  bool AreTerritoriesAdjacent(const ATerritory *A, const ATerritory *B) const;

  /** Determine if a territory is owned by a given player state. */
  UFUNCTION(BlueprintCallable, Category = "WorldMap")
  bool IsOwnedBy(const ATerritory *Territory,
                 const ASkaldPlayerState *Player) const;

  /** Automatically distribute remaining deployable units across owned territories. */
  UFUNCTION(BlueprintCallable, Category = "WorldMap")
  int32 AutoPlaceUnitsForAI(ASkaldPlayerState *PlayerState);

protected:
  /** Whether the overworld should currently be visible and interactive. */
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "WorldMap")
  bool bIsWorldActive = true;

private:
  /** Cached collision state so we can restore original settings after battles. */
  TMap<TWeakObjectPtr<UPrimitiveComponent>, TEnumAsByte<ECollisionEnabled::Type>>
      CachedCollisionStates;

  /** Cached audio playback state for pausing/resuming overworld ambience. */
  TMap<TWeakObjectPtr<UAudioComponent>, bool> CachedAudioPlaybackState;

  /** Territory IDs marked as capital candidates in the data table. */
  TSet<int32> CapitalCandidateIDs;
};
