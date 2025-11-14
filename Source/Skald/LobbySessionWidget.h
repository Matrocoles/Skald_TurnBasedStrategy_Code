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
    UPROPERTY(meta=(BindWidgetOptional))
    UVerticalBox* RootPanel = nullptr;

    /** Horizontal container holding the four slot panels. */
    UPROPERTY(meta=(BindWidgetOptional))
    UHorizontalBox* SlotsPanel = nullptr;

    /** Host controls for player/AI counts and launching. */
    UPROPERTY(meta=(BindWidgetOptional))
    UHorizontalBox* HostControls = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    USpinBox* PlayerCountSpinBox = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    USpinBox* AICountSpinBox = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UButton* LaunchButton = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* LaunchLabel = nullptr;

    /** Slot 0 widget bindings for the designer. */
    UPROPERTY(meta=(BindWidgetOptional))
    UBorder* Slot0Container = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* Slot0Title = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UEditableTextBox* Slot0NameEdit = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UComboBoxString* Slot0FactionCombo = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UButton* Slot0ReadyButton = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* Slot0ReadyLabel = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* Slot0StatusText = nullptr;

    /** Slot 1 widget bindings for the designer. */
    UPROPERTY(meta=(BindWidgetOptional))
    UBorder* Slot1Container = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* Slot1Title = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UEditableTextBox* Slot1NameEdit = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UComboBoxString* Slot1FactionCombo = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UButton* Slot1ReadyButton = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* Slot1ReadyLabel = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* Slot1StatusText = nullptr;

    /** Slot 2 widget bindings for the designer. */
    UPROPERTY(meta=(BindWidgetOptional))
    UBorder* Slot2Container = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* Slot2Title = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UEditableTextBox* Slot2NameEdit = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UComboBoxString* Slot2FactionCombo = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UButton* Slot2ReadyButton = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* Slot2ReadyLabel = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* Slot2StatusText = nullptr;

    /** Slot 3 widget bindings for the designer. */
    UPROPERTY(meta=(BindWidgetOptional))
    UBorder* Slot3Container = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* Slot3Title = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UEditableTextBox* Slot3NameEdit = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UComboBoxString* Slot3FactionCombo = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UButton* Slot3ReadyButton = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* Slot3ReadyLabel = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* Slot3StatusText = nullptr;

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

    /** True once the widget has received an initial replicated snapshot. */
    bool bHasReceivedInitialState = false;

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

    UFUNCTION()
    void HandlePlayerCountChanged(float Value);

    UFUNCTION()
    void HandleAICountChanged(float Value);

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

