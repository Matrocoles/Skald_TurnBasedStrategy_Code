#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Skald_PlayerController.generated.h"

class ATurnManager;

/**
 * Player controller capable of participating in turn based gameplay.
 */
UCLASS()
class SKALD_API ASkaldPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    ASkaldPlayerController();

    virtual void BeginPlay() override;

    void StartTurn();
    void EndTurn();
    void MakeAIDecision();
    bool IsAIController() const { return bIsAI; }
    void SetTurnManager(ATurnManager* Manager);

protected:
    /** Whether this controller is controlled by AI. */
    bool bIsAI;

    UPROPERTY()
    ATurnManager* TurnManager;
};

