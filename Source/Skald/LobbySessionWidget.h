#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LobbyTypes.h"
#include "LobbySessionWidget.generated.h"

class UBorder;
class UButton;
class UComboBoxString;
class UEditableTextBox;
class UHorizontalBox;
class USpinBox;
class UTextBlock;
class UVerticalBox;
class ALobbyGameState;
class ALobbyPlayerController;

USTRUCT()
struct FLobbySlotWidgets
{
    GENERATED_BODY()

    UPROPERTY()
    UBorder* Container = nullptr;

    UPROPERTY()
    UTextBlock* Title = nullptr;

    UPROPERTY()
    UEditableTextBox* NameEdit = nullptr;

    UPROPERTY()
    UComboBoxString* FactionCombo = nullptr;

    UPROPERTY()
    UButton* ReadyButton = nullptr;

    UPROPERTY()
    UTextBlock* ReadyLabel = nullptr;

    UPROPERTY()
    UTextBlock* StatusText = nullptr;
};

/** Multiplayer lobby widget showing player slots and host controls. */
UCLASS()
class SKALD_API ULobbySessionWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    ULobbySessionWidget(const FObjectInitializer& ObjectInitializer);

    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

protected:

    /** Root panel for the entire widget. */
    UPROPERTY()
    UVerticalBox* RootPanel = nullptr;

    /** Horizontal container holding the four slot panels. */
    UPROPERTY()
    UHorizontalBox* SlotsPanel = nullptr;

    /** Host controls for player/AI counts and launching. */
    UPROPERTY()
    UHorizontalBox* HostControls = nullptr;

    UPROPERTY()
    USpinBox* PlayerCountSpinBox = nullptr;

    UPROPERTY()
    USpinBox* AICountSpinBox = nullptr;

    UPROPERTY()
    UButton* LaunchButton = nullptr;

    UPROPERTY()
    UTextBlock* LaunchLabel = nullptr;

    /** Cached slot widgets for updating. */
    UPROPERTY()
    TArray<FLobbySlotWidgets> SlotWidgets;

    /** Cached reference to the lobby game state. */
    UPROPERTY()
    ALobbyGameState* CachedGameState = nullptr;

    /** Cached reference to the owning lobby controller. */
    UPROPERTY()
    ALobbyPlayerController* CachedController = nullptr;

    /** Whether the widget is currently applying replicated state. */
    bool bIsUpdatingFromState = false;

    void BuildLayout();
    void RebuildFactionOptions(UComboBoxString* Combo) const;
    void RefreshFromState();
    void RefreshSlot(int32 SlotIndex, const FLobbyPlayerSlot& Slot, int32 LocalSlotIndex);
    void UpdateHostControls(int32 TotalSlots, int32 AISlots, bool bAllReady);

    int32 ResolveLocalSlotIndex() const;

    UFUNCTION()
    void HandleLobbySlotsUpdated();

    UFUNCTION()
    void HandlePlayerCountCommitted(float Value, ETextCommit::Type CommitType);

    UFUNCTION()
    void HandleAICountCommitted(float Value, ETextCommit::Type CommitType);

    void HandleReadyClicked(int32 SlotIndex);
    void HandleFactionSelected(int32 SlotIndex, const FString& SelectedItem, ESelectInfo::Type SelectionType);
    void HandleNameCommitted(int32 SlotIndex, const FText& Text, ETextCommit::Type CommitType);

    UFUNCTION()
    void HandleReadyClickedSlot0();
    UFUNCTION()
    void HandleReadyClickedSlot1();
    UFUNCTION()
    void HandleReadyClickedSlot2();
    UFUNCTION()
    void HandleReadyClickedSlot3();

    UFUNCTION()
    void HandleFactionSelectedSlot0(FString SelectedItem, ESelectInfo::Type SelectionType);
    UFUNCTION()
    void HandleFactionSelectedSlot1(FString SelectedItem, ESelectInfo::Type SelectionType);
    UFUNCTION()
    void HandleFactionSelectedSlot2(FString SelectedItem, ESelectInfo::Type SelectionType);
    UFUNCTION()
    void HandleFactionSelectedSlot3(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void HandleNameCommittedSlot0(const FText& Text, ETextCommit::Type CommitType);
    UFUNCTION()
    void HandleNameCommittedSlot1(const FText& Text, ETextCommit::Type CommitType);
    UFUNCTION()
    void HandleNameCommittedSlot2(const FText& Text, ETextCommit::Type CommitType);
    UFUNCTION()
    void HandleNameCommittedSlot3(const FText& Text, ETextCommit::Type CommitType);

    UFUNCTION()
    void HandleLaunchClicked();
};

