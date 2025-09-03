#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"
#include "CoreMinimal.h"
#include "FighterPawn.generated.h"
#include "GameFramework/Pawn.h"
#include "GridBattleManager.h"

class UGridOverlayComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthChanged, int32, NewHealth);

/** Pawn representing a fighter in grid battles. */
UCLASS()
class SKALD_API AFighterPawn : public APawn {
  GENERATED_BODY()

public:
  AFighterPawn();

  virtual void BeginPlay() override;

  /** Prepare the fighter for its activation. */
  UFUNCTION(BlueprintCallable, Category = "Fighter")
  void BeginActivation();

  /** Move to the specified grid cell if actions remain. */
  UFUNCTION(BlueprintCallable, Category = "Fighter")
  void MoveToCell(FIntPoint TargetCell);

  /** Perform an attack against another fighter. */
  UFUNCTION(BlueprintCallable, Category = "Fighter")
  void PerformAttack(AFighterPawn *Target);

  /** Check whether this fighter is still alive. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Fighter")
  bool IsAlive() const;

  /** Statistics describing this fighter. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Fighter")
  FFighterStats Stats;

  /** Actions remaining for the current activation. */
  UPROPERTY(BlueprintReadOnly, Category = "Fighter")
  int32 ActionsRemaining;

  /** Mesh used to display the fighter. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
  UStaticMeshComponent *DisplayMesh;

  /** Widget displaying the current health. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|UI")
  UWidgetComponent *HealthWidget;

  /** Widget class used for the health display. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fighter|UI")
  TSubclassOf<UUserWidget> HealthWidgetTemplate;

  /** Widget class used for optional damage float indicators. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fighter|UI")
  TSubclassOf<UUserWidget> DamageFloatWidgetTemplate;

  /** Event broadcast when health changes. */
  UPROPERTY(BlueprintAssignable, Category = "Fighter|Events")
  FOnHealthChanged OnHealthChanged;

protected:
  /** Clear grid occupancy when the fighter is destroyed. */
  virtual void Destroyed() override;

private:
  /** Update the health widget with a new value. */
  UFUNCTION()
  void UpdateHealthDisplay(int32 NewHealth);

  /** Get the grid overlay component, caching the result. */
  UGridOverlayComponent *GetGrid();

  /** Current cell occupied by the fighter. */
  FIntPoint CurrentCell;

  /** Cached grid overlay component. */
  UGridOverlayComponent *CachedGrid = nullptr;
};
