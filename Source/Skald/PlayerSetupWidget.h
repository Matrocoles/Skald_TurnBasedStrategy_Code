#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkaldTypes.h"
#include "PlayerSetupWidget.generated.h"

/**
 * Widget shown after selecting single or multiplayer that allows the
 * player to choose a display name and faction before entering the game.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API UPlayerSetupWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;

    /** Called when the player confirms their choices. */
    UFUNCTION(BlueprintCallable)
    void OnConfirm();

    /** Set the faction chosen from the UI. */
    UFUNCTION(BlueprintCallable)
    void OnFactionSelected(EFaction NewFaction);

    /** Display name entered by the player. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Setup")
    FString DisplayName;

    /** Currently selected faction. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Setup")
    EFaction SelectedFaction;

    /** Whether the game should start in multiplayer. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Setup")
    bool bMultiplayer;
};

