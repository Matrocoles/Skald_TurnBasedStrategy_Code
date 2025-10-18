#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CombatFloaterPoolSubsystem.generated.h"

class APlayerController;
class UW_FloatingText;
class UUserWidget;

/**
 * Subsystem that manages pooled floating combat text widgets to minimise
 * allocation churn during hectic encounters.
 */
UCLASS()
class SKALD_API UCombatFloaterPoolSubsystem : public UWorldSubsystem {
  GENERATED_BODY()

public:
  UCombatFloaterPoolSubsystem();

  virtual void Deinitialize() override;

  /** Widget class spawned when the pool requires new floaters. */
  UPROPERTY(EditAnywhere, Category = "Skald|UI|Floaters")
  TSubclassOf<UW_FloatingText> FloaterWidgetClass;

  /**
   * Retrieve an active floater widget for the supplied player controller.
   * If the pool is empty a new instance will be created.
   */
  UW_FloatingText *SpawnFloater(APlayerController *OwningPlayer);

  /**
   * Return a floater to the inactive pool so it can be reused later.
   */
  void ReleaseFloater(UW_FloatingText *FloaterWidget);

private:
  /**
   * Weak references to widgets currently available for reuse.
   */
  UPROPERTY()
  TArray<TWeakObjectPtr<UUserWidget>> InactivePool;

  /** Strong references to every floater we have spawned so far. */
  UPROPERTY()
  TArray<TObjectPtr<UUserWidget>> SpawnedWidgets;

  /** Resolve the widget class to use when creating new floaters. */
  UClass *ResolveFloaterClass() const;
};

