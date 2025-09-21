#pragma once

#include "CoreMinimal.h"
#include "Skald_PlayerController.h"
#include "Skald_AIController.generated.h"

/**
 * Controller handling AI turn logic.
 */
UCLASS()
class SKALD_API ASkaldAIController : public ASkaldPlayerController {
  GENERATED_BODY()

public:
  virtual void BeginPlay() override;
  virtual void StartTurn() override;

  /** Execute the AI's decision making for the current turn. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void MakeAIDecision();
};

