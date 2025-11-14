#include "LobbySessionWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Internationalization/Text.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "LobbyGameState.h"
#include "LobbyPlayerController.h"
#include "Skald_PlayerState.h"
#include "Styling/SlateColor.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
constexpr const TCHAR* FactionPlaceholder = TEXT("Select Faction");
FLinearColor InactiveSlotColor(0.05f, 0.05f, 0.05f, 0.9f);
FLinearColor ActiveSlotColor(0.08f, 0.08f, 0.1f, 0.95f);
FLinearColor ReadyColor(0.2f, 0.6f, 0.2f, 1.0f);
FLinearColor NotReadyColor(0.7f, 0.55f, 0.1f, 1.0f);
} // namespace

ULobbySessionWidget::ULobbySessionWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SlotWidgets.SetNum(4);
}

void ULobbySessionWidget::NativeConstruct()
{
    Super::NativeConstruct();

    CachedController = GetOwningPlayer<ALobbyPlayerController>();
    CachedGameState = GetWorld() ? GetWorld()->GetGameState<ALobbyGameState>() : nullptr;
    bHasReceivedInitialState = false;

    BuildLayout();

    if (CachedGameState)
    {
        CachedGameState->OnLobbySlotsUpdated.AddDynamic(this, &ULobbySessionWidget::HandleLobbySlotsUpdated);
    }

    RefreshFromState();
}

void ULobbySessionWidget::NativeDestruct()
{
    if (CachedGameState)
    {
        CachedGameState->OnLobbySlotsUpdated.RemoveDynamic(this, &ULobbySessionWidget::HandleLobbySlotsUpdated);
    }

    Super::NativeDestruct();
}

void ULobbySessionWidget::BuildLayout()
{
    if (!WidgetTree)
    {
        return;
    }

    SlotWidgets.SetNum(4);

    const bool bNeedsProgrammaticLayout = !RootPanel || !SlotsPanel || !HostControls;

    if (bNeedsProgrammaticLayout)
    {
        RootPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RootPanel"));
        WidgetTree->RootWidget = RootPanel;

        SlotsPanel = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SlotsPanel"));
        RootPanel->AddChild(SlotsPanel);

        for (int32 Index = 0; Index < SlotWidgets.Num(); ++Index)
        {
            UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
            Border->SetPadding(FMargin(8.f));
            Border->SetBrushColor(InactiveSlotColor);

            UVerticalBox* SlotBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
            Border->SetContent(SlotBox);

            UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
            Title->SetText(FText::Format(NSLOCTEXT("Lobby", "SlotTitle", "Player {0}"), FText::AsNumber(Index + 1)));
            Title->SetJustification(ETextJustify::Center);
            SlotBox->AddChildToVerticalBox(Title);

            UEditableTextBox* NameEdit = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
            NameEdit->SetHintText(NSLOCTEXT("Lobby", "NameHint", "Display Name"));
            SlotBox->AddChildToVerticalBox(NameEdit);

            UComboBoxString* FactionCombo = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass());
            RebuildFactionOptions(FactionCombo);
            FactionCombo->SetSelectedOption(FactionPlaceholder);
            SlotBox->AddChildToVerticalBox(FactionCombo);

            UHorizontalBox* ReadyRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
            SlotBox->AddChildToVerticalBox(ReadyRow);

            UButton* ReadyButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
            UHorizontalBoxSlot* ReadyButtonSlot = ReadyRow->AddChildToHorizontalBox(ReadyButton);
            ReadyButtonSlot->SetPadding(FMargin(0.f, 4.f, 12.f, 0.f));

            UTextBlock* ReadyLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
            ReadyLabel->SetText(NSLOCTEXT("Lobby", "Ready", "Ready"));
            ReadyButton->SetContent(ReadyLabel);

            UTextBlock* StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
            StatusText->SetText(NSLOCTEXT("Lobby", "StatusNotReady", "Not Ready"));
            ReadyRow->AddChildToHorizontalBox(StatusText);

            UHorizontalBoxSlot* SlotContainer = SlotsPanel->AddChildToHorizontalBox(Border);
            SlotContainer->SetPadding(FMargin(4.f));
            SlotContainer->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

            switch (Index)
            {
            case 0:
                Slot0Container = Border;
                Slot0Title = Title;
                Slot0NameEdit = NameEdit;
                Slot0FactionCombo = FactionCombo;
                Slot0ReadyButton = ReadyButton;
                Slot0ReadyLabel = ReadyLabel;
                Slot0StatusText = StatusText;
                break;
            case 1:
                Slot1Container = Border;
                Slot1Title = Title;
                Slot1NameEdit = NameEdit;
                Slot1FactionCombo = FactionCombo;
                Slot1ReadyButton = ReadyButton;
                Slot1ReadyLabel = ReadyLabel;
                Slot1StatusText = StatusText;
                break;
            case 2:
                Slot2Container = Border;
                Slot2Title = Title;
                Slot2NameEdit = NameEdit;
                Slot2FactionCombo = FactionCombo;
                Slot2ReadyButton = ReadyButton;
                Slot2ReadyLabel = ReadyLabel;
                Slot2StatusText = StatusText;
                break;
            case 3:
                Slot3Container = Border;
                Slot3Title = Title;
                Slot3NameEdit = NameEdit;
                Slot3FactionCombo = FactionCombo;
                Slot3ReadyButton = ReadyButton;
                Slot3ReadyLabel = ReadyLabel;
                Slot3StatusText = StatusText;
                break;
            default:
                break;
            }
        }

        HostControls = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HostControls"));
        RootPanel->AddChild(HostControls);

        auto AddLabel = [this](UHorizontalBox* Target, const FText& InText)
        {
            UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
            Label->SetText(InText);
            Label->SetMargin(FMargin(8.f, 12.f, 8.f, 0.f));
            Target->AddChildToHorizontalBox(Label);
            return Label;
        };

        AddLabel(HostControls, NSLOCTEXT("Lobby", "PlayerCount", "Players:"));

        PlayerCountSpinBox = WidgetTree->ConstructWidget<USpinBox>(USpinBox::StaticClass());
        PlayerCountSpinBox->SetMinValue(2.f);
        PlayerCountSpinBox->SetMaxValue(4.f);
        PlayerCountSpinBox->SetDelta(1.f);
        HostControls->AddChildToHorizontalBox(PlayerCountSpinBox);

        AddLabel(HostControls, NSLOCTEXT("Lobby", "AICount", "AI:"));

        AICountSpinBox = WidgetTree->ConstructWidget<USpinBox>(USpinBox::StaticClass());
        AICountSpinBox->SetMinValue(0.f);
        AICountSpinBox->SetMaxValue(3.f);
        AICountSpinBox->SetDelta(1.f);
        HostControls->AddChildToHorizontalBox(AICountSpinBox);

        LaunchButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
        LaunchLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        LaunchLabel->SetText(NSLOCTEXT("Lobby", "Launch", "Launch Game"));
        LaunchButton->SetContent(LaunchLabel);
        HostControls->AddChildToHorizontalBox(LaunchButton);
    }
    else if (RootPanel)
    {
        WidgetTree->RootWidget = RootPanel;
    }

    auto ConfigureSlot = [this](int32 Index, UBorder* Container, UTextBlock* Title, UEditableTextBox* NameEdit, UComboBoxString* FactionCombo, UButton* ReadyButton, UTextBlock* ReadyLabel, UTextBlock* StatusText)
    {
        if (!SlotWidgets.IsValidIndex(Index))
        {
            return;
        }

        FLobbySlotWidgets& SlotWidget = SlotWidgets[Index];
        SlotWidget.Container = Container;
        SlotWidget.Title = Title;
        SlotWidget.NameEdit = NameEdit;
        SlotWidget.FactionCombo = FactionCombo;
        SlotWidget.ReadyButton = ReadyButton;
        SlotWidget.ReadyLabel = ReadyLabel;
        SlotWidget.StatusText = StatusText;

        auto EnsurePlaceholder = [](UComboBoxString* Combo)
        {
            if (!Combo)
            {
                return;
            }

            const int32 PlaceholderIndex = Combo->FindOptionIndex(FactionPlaceholder);
            if (PlaceholderIndex == INDEX_NONE)
            {
                const FString CurrentSelection = Combo->GetSelectedOption();

                TArray<FString> ExistingOptions;
                ExistingOptions.Reserve(Combo->GetOptionCount());
                for (int32 OptionIndex = 0; OptionIndex < Combo->GetOptionCount(); ++OptionIndex)
                {
                    ExistingOptions.Add(Combo->GetOptionAtIndex(OptionIndex));
                }

                Combo->ClearOptions();
                Combo->AddOption(FactionPlaceholder);

                for (const FString& Option : ExistingOptions)
                {
                    Combo->AddOption(Option);
                }

                Combo->RefreshOptions();

                if (!CurrentSelection.IsEmpty())
                {
                    if (ExistingOptions.Contains(CurrentSelection))
                    {
                        Combo->SetSelectedOption(CurrentSelection);
                    }
                    else
                    {
                        Combo->SetSelectedOption(FactionPlaceholder);
                    }
                }
                else
                {
                    Combo->SetSelectedOption(FactionPlaceholder);
                }
            }
        };

        EnsurePlaceholder(FactionCombo);

        switch (Index)
        {
        case 0:
            if (NameEdit)
            {
                NameEdit->OnTextCommitted.RemoveDynamic(this, &ULobbySessionWidget::HandleNameCommittedSlot0);
                NameEdit->OnTextCommitted.AddDynamic(this, &ULobbySessionWidget::HandleNameCommittedSlot0);
            }
            if (FactionCombo)
            {
                if (FactionCombo->GetOptionCount() <= 1)
                {
                    RebuildFactionOptions(FactionCombo);
                }
                if (FactionCombo->GetSelectedOption().IsEmpty())
                {
                    FactionCombo->SetSelectedOption(FactionPlaceholder);
                }
                FactionCombo->OnSelectionChanged.RemoveDynamic(this, &ULobbySessionWidget::HandleFactionSelectedSlot0);
                FactionCombo->OnSelectionChanged.AddDynamic(this, &ULobbySessionWidget::HandleFactionSelectedSlot0);
            }
            if (ReadyButton)
            {
                ReadyButton->OnClicked.RemoveDynamic(this, &ULobbySessionWidget::HandleReadyClickedSlot0);
                ReadyButton->OnClicked.AddDynamic(this, &ULobbySessionWidget::HandleReadyClickedSlot0);
            }
            break;
        case 1:
            if (NameEdit)
            {
                NameEdit->OnTextCommitted.RemoveDynamic(this, &ULobbySessionWidget::HandleNameCommittedSlot1);
                NameEdit->OnTextCommitted.AddDynamic(this, &ULobbySessionWidget::HandleNameCommittedSlot1);
            }
            if (FactionCombo)
            {
                if (FactionCombo->GetOptionCount() <= 1)
                {
                    RebuildFactionOptions(FactionCombo);
                }
                if (FactionCombo->GetSelectedOption().IsEmpty())
                {
                    FactionCombo->SetSelectedOption(FactionPlaceholder);
                }
                FactionCombo->OnSelectionChanged.RemoveDynamic(this, &ULobbySessionWidget::HandleFactionSelectedSlot1);
                FactionCombo->OnSelectionChanged.AddDynamic(this, &ULobbySessionWidget::HandleFactionSelectedSlot1);
            }
            if (ReadyButton)
            {
                ReadyButton->OnClicked.RemoveDynamic(this, &ULobbySessionWidget::HandleReadyClickedSlot1);
                ReadyButton->OnClicked.AddDynamic(this, &ULobbySessionWidget::HandleReadyClickedSlot1);
            }
            break;
        case 2:
            if (NameEdit)
            {
                NameEdit->OnTextCommitted.RemoveDynamic(this, &ULobbySessionWidget::HandleNameCommittedSlot2);
                NameEdit->OnTextCommitted.AddDynamic(this, &ULobbySessionWidget::HandleNameCommittedSlot2);
            }
            if (FactionCombo)
            {
                if (FactionCombo->GetOptionCount() <= 1)
                {
                    RebuildFactionOptions(FactionCombo);
                }
                if (FactionCombo->GetSelectedOption().IsEmpty())
                {
                    FactionCombo->SetSelectedOption(FactionPlaceholder);
                }
                FactionCombo->OnSelectionChanged.RemoveDynamic(this, &ULobbySessionWidget::HandleFactionSelectedSlot2);
                FactionCombo->OnSelectionChanged.AddDynamic(this, &ULobbySessionWidget::HandleFactionSelectedSlot2);
            }
            if (ReadyButton)
            {
                ReadyButton->OnClicked.RemoveDynamic(this, &ULobbySessionWidget::HandleReadyClickedSlot2);
                ReadyButton->OnClicked.AddDynamic(this, &ULobbySessionWidget::HandleReadyClickedSlot2);
            }
            break;
        case 3:
            if (NameEdit)
            {
                NameEdit->OnTextCommitted.RemoveDynamic(this, &ULobbySessionWidget::HandleNameCommittedSlot3);
                NameEdit->OnTextCommitted.AddDynamic(this, &ULobbySessionWidget::HandleNameCommittedSlot3);
            }
            if (FactionCombo)
            {
                if (FactionCombo->GetOptionCount() <= 1)
                {
                    RebuildFactionOptions(FactionCombo);
                }
                if (FactionCombo->GetSelectedOption().IsEmpty())
                {
                    FactionCombo->SetSelectedOption(FactionPlaceholder);
                }
                FactionCombo->OnSelectionChanged.RemoveDynamic(this, &ULobbySessionWidget::HandleFactionSelectedSlot3);
                FactionCombo->OnSelectionChanged.AddDynamic(this, &ULobbySessionWidget::HandleFactionSelectedSlot3);
            }
            if (ReadyButton)
            {
                ReadyButton->OnClicked.RemoveDynamic(this, &ULobbySessionWidget::HandleReadyClickedSlot3);
                ReadyButton->OnClicked.AddDynamic(this, &ULobbySessionWidget::HandleReadyClickedSlot3);
            }
            break;
        default:
            break;
        }
    };

    ConfigureSlot(0, Slot0Container, Slot0Title, Slot0NameEdit, Slot0FactionCombo, Slot0ReadyButton, Slot0ReadyLabel, Slot0StatusText);
    ConfigureSlot(1, Slot1Container, Slot1Title, Slot1NameEdit, Slot1FactionCombo, Slot1ReadyButton, Slot1ReadyLabel, Slot1StatusText);
    ConfigureSlot(2, Slot2Container, Slot2Title, Slot2NameEdit, Slot2FactionCombo, Slot2ReadyButton, Slot2ReadyLabel, Slot2StatusText);
    ConfigureSlot(3, Slot3Container, Slot3Title, Slot3NameEdit, Slot3FactionCombo, Slot3ReadyButton, Slot3ReadyLabel, Slot3StatusText);

    if (PlayerCountSpinBox)
    {
        PlayerCountSpinBox->OnValueChanged.RemoveDynamic(this, &ULobbySessionWidget::HandlePlayerCountChanged);
        PlayerCountSpinBox->OnValueChanged.AddDynamic(this, &ULobbySessionWidget::HandlePlayerCountChanged);
        PlayerCountSpinBox->OnValueCommitted.RemoveDynamic(this, &ULobbySessionWidget::HandlePlayerCountCommitted);
        PlayerCountSpinBox->OnValueCommitted.AddDynamic(this, &ULobbySessionWidget::HandlePlayerCountCommitted);
    }

    if (AICountSpinBox)
    {
        AICountSpinBox->OnValueChanged.RemoveDynamic(this, &ULobbySessionWidget::HandleAICountChanged);
        AICountSpinBox->OnValueChanged.AddDynamic(this, &ULobbySessionWidget::HandleAICountChanged);
        AICountSpinBox->OnValueCommitted.RemoveDynamic(this, &ULobbySessionWidget::HandleAICountCommitted);
        AICountSpinBox->OnValueCommitted.AddDynamic(this, &ULobbySessionWidget::HandleAICountCommitted);
    }

    if (LaunchButton)
    {
        LaunchButton->OnClicked.RemoveDynamic(this, &ULobbySessionWidget::HandleLaunchClicked);
        LaunchButton->OnClicked.AddDynamic(this, &ULobbySessionWidget::HandleLaunchClicked);
    }
}

void ULobbySessionWidget::RebuildFactionOptions(UComboBoxString* Combo) const
{
    if (!Combo)
    {
        return;
    }

    Combo->ClearOptions();
    Combo->AddOption(FactionPlaceholder);

    if (UEnum* Enum = StaticEnum<ESkaldFaction>())
    {
        for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
        {
            const int64 Value = Enum->GetValueByIndex(Index);
            if (!Enum->IsValidEnumValue(Value))
            {
                continue;
            }

            const FString Name = Enum->GetNameStringByIndex(Index);
            if (Name == TEXT("None") || Name.EndsWith(TEXT("_MAX")))
            {
                continue;
            }

            Combo->AddOption(Name);
        }
    }

    Combo->RefreshOptions();
}

void ULobbySessionWidget::RefreshFromState()
{
    if (!CachedGameState)
    {
        CachedGameState = GetWorld() ? GetWorld()->GetGameState<ALobbyGameState>() : nullptr;
        if (CachedGameState)
        {
            CachedGameState->OnLobbySlotsUpdated.AddDynamic(this, &ULobbySessionWidget::HandleLobbySlotsUpdated);
        }
    }

    const int32 LocalSlotIndex = ResolveLocalSlotIndex();

    bIsUpdatingFromState = true;

    if (CachedGameState)
    {
        int32 ActiveSlots = 0;
        for (int32 Index = 0; Index < SlotWidgets.Num(); ++Index)
        {
            const FLobbyPlayerSlot* SlotData = CachedGameState->GetSlot(Index);
            if (SlotData && SlotData->bIsActive)
            {
                ++ActiveSlots;
            }
        }

        if (!bHasReceivedInitialState && ActiveSlots > 0)
        {
            bHasReceivedInitialState = true;
        }

        for (int32 Index = 0; Index < SlotWidgets.Num(); ++Index)
        {
            const FLobbyPlayerSlot* SlotData = CachedGameState->GetSlot(Index);
            if (SlotData)
            {
                RefreshSlot(Index, *SlotData, LocalSlotIndex);
            }
        }

        UpdateHostControls(CachedGameState->TotalSlots, CachedGameState->ReservedAISlots, CachedGameState->AreAllSlotsReady());
    }

    bIsUpdatingFromState = false;
}

void ULobbySessionWidget::RefreshSlot(int32 SlotIndex, const FLobbyPlayerSlot& SlotData, int32 LocalSlotIndex)
{
    if (!SlotWidgets.IsValidIndex(SlotIndex))
    {
        return;
    }

    FLobbySlotWidgets& Widgets = SlotWidgets[SlotIndex];
    if (!Widgets.Container)
    {
        return;
    }

    Widgets.Container->SetBrushColor(SlotData.bIsActive ? ActiveSlotColor : InactiveSlotColor);

    const bool bIsLocal = SlotIndex == LocalSlotIndex;
    const bool bIsAI = SlotData.bIsAI;
    const bool bCanInteract = SlotData.bIsActive && !bIsAI && bIsLocal &&
        !SlotData.bIsReady && bHasReceivedInitialState;

    if (Widgets.NameEdit)
    {
        Widgets.NameEdit->SetIsEnabled(bCanInteract);
        Widgets.NameEdit->SetText(FText::FromString(SlotData.DisplayName));
    }

    if (Widgets.FactionCombo)
    {
        Widgets.FactionCombo->ClearSelection();
        if (SlotData.Faction != ESkaldFaction::None)
        {
            if (UEnum* Enum = StaticEnum<ESkaldFaction>())
            {
                const FString Name = Enum->GetNameStringByValue(static_cast<int64>(SlotData.Faction));
                Widgets.FactionCombo->SetSelectedOption(Name);
            }
        }
        else
        {
            Widgets.FactionCombo->SetSelectedOption(FactionPlaceholder);
        }

        Widgets.FactionCombo->SetIsEnabled(bCanInteract);
    }

    if (Widgets.ReadyButton && Widgets.ReadyLabel)
    {
        const bool bShowReadyButton = SlotData.bIsActive && !bIsAI && bHasReceivedInitialState;
        Widgets.ReadyButton->SetVisibility(bShowReadyButton ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        Widgets.ReadyButton->SetIsEnabled(bCanInteract);
        Widgets.ReadyLabel->SetText(SlotData.bIsReady ? NSLOCTEXT("Lobby", "LockedLabel", "Locked") : NSLOCTEXT("Lobby", "LockInAction", "Lock In"));
    }

    if (Widgets.StatusText)
    {
        if (!SlotData.bIsActive)
        {
            Widgets.StatusText->SetText(NSLOCTEXT("Lobby", "Inactive", "Inactive"));
            Widgets.StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::Gray));
        }
        else if (bIsAI)
        {
            Widgets.StatusText->SetText(NSLOCTEXT("Lobby", "AILocked", "AI Locked"));
            Widgets.StatusText->SetColorAndOpacity(FSlateColor(ReadyColor));
        }
        else if (bHasReceivedInitialState)
        {
            Widgets.StatusText->SetText(SlotData.bIsReady ? NSLOCTEXT("Lobby", "LockedStatus", "Locked In") : NSLOCTEXT("Lobby", "WaitingStatus", "Awaiting Lock In"));
            Widgets.StatusText->SetColorAndOpacity(FSlateColor(SlotData.bIsReady ? ReadyColor : NotReadyColor));
        }
        else
        {
            Widgets.StatusText->SetText(NSLOCTEXT("Lobby", "Loading", "Loading..."));
            Widgets.StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::Gray));
        }
    }
}

void ULobbySessionWidget::UpdateHostControls(int32 TotalSlots, int32 AISlots, bool bAllReady)
{
    const bool bIsHost = CachedController && CachedController->IsLocalPlayerLobbyHost();
    const bool bConfigurationLocked = CachedGameState && CachedGameState->bSlotConfigurationLocked;
    const bool bHasState = bHasReceivedInitialState;
    const bool bShowControls = bIsHost && bHasState;

    if (PlayerCountSpinBox)
    {
        PlayerCountSpinBox->SetValue(static_cast<float>(TotalSlots));
        PlayerCountSpinBox->SetIsEnabled(bShowControls && !bConfigurationLocked);
    }

    if (AICountSpinBox)
    {
        AICountSpinBox->SetMaxValue(static_cast<float>(FMath::Max(0, TotalSlots - 1)));
        AICountSpinBox->SetValue(static_cast<float>(AISlots));
        AICountSpinBox->SetIsEnabled(bShowControls && !bConfigurationLocked);
    }

    if (HostControls)
    {
        HostControls->SetVisibility(bShowControls ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }

    if (LaunchButton)
    {
        LaunchButton->SetIsEnabled(bShowControls && bAllReady);
        LaunchButton->SetVisibility(bShowControls ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }
}

int32 ULobbySessionWidget::ResolveLocalSlotIndex() const
{
    if (!CachedController)
    {
        return INDEX_NONE;
    }

    if (ALobbyGameState* State = CachedGameState)
    {
        if (ASkaldPlayerState* PS = CachedController->GetPlayerState<ASkaldPlayerState>())
        {
            return State->FindSlotIndexForPlayer(PS->GetPlayerId());
        }
    }

    return INDEX_NONE;
}

void ULobbySessionWidget::HandleLobbySlotsUpdated()
{
    RefreshFromState();
}

void ULobbySessionWidget::HandlePlayerCountCommitted(float Value, ETextCommit::Type CommitType)
{
    if (CommitType == ETextCommit::OnEnter)
    {
        HandlePlayerCountChanged(Value);
    }
}

void ULobbySessionWidget::HandleAICountCommitted(float Value, ETextCommit::Type CommitType)
{
    if (CommitType == ETextCommit::OnEnter)
    {
        HandleAICountChanged(Value);
    }
}

void ULobbySessionWidget::HandlePlayerCountChanged(float Value)
{
    if (!bIsUpdatingFromState && CachedController)
    {
        CachedController->RequestPlayerCount(FMath::RoundToInt(Value));
    }
}

void ULobbySessionWidget::HandleAICountChanged(float Value)
{
    if (!bIsUpdatingFromState && CachedController)
    {
        CachedController->RequestAICount(FMath::RoundToInt(Value));
    }
}

void ULobbySessionWidget::HandleReadyClicked(int32 SlotIndex)
{
    if (!CachedGameState || !CachedController)
    {
        return;
    }

    const int32 LocalSlot = ResolveLocalSlotIndex();
    if (LocalSlot != SlotIndex)
    {
        return;
    }

    if (const FLobbyPlayerSlot* SlotData = CachedGameState->GetSlot(SlotIndex))
    {
        if (!SlotData->bIsReady)
        {
            CachedController->RequestLockIn();
        }
    }
}

void ULobbySessionWidget::HandleFactionSelected(int32 SlotIndex, const FString& SelectedItem, ESelectInfo::Type SelectionType)
{
    if (bIsUpdatingFromState || !CachedController)
    {
        return;
    }

    const int32 LocalSlot = ResolveLocalSlotIndex();
    if (LocalSlot != SlotIndex)
    {
        return;
    }

    if (SelectedItem == FactionPlaceholder)
    {
        return;
    }

    if (UEnum* Enum = StaticEnum<ESkaldFaction>())
    {
        const int64 Value = Enum->GetValueByNameString(SelectedItem);
        if (Value != INDEX_NONE)
        {
            CachedController->RequestFactionSelection(static_cast<ESkaldFaction>(Value));
        }
    }
}

void ULobbySessionWidget::HandleNameCommitted(int32 SlotIndex, const FText& Text, ETextCommit::Type CommitType)
{
    if (bIsUpdatingFromState || !CachedController)
    {
        return;
    }

    const int32 LocalSlot = ResolveLocalSlotIndex();
    if (LocalSlot != SlotIndex)
    {
        return;
    }

    CachedController->RequestDisplayNameUpdate(Text.ToString());
}

void ULobbySessionWidget::HandleLaunchClicked()
{
    if (CachedController)
    {
        CachedController->RequestLaunch();
    }
}

void ULobbySessionWidget::HandleReadyClickedSlot0()
{
    HandleReadyClicked(0);
}

void ULobbySessionWidget::HandleReadyClickedSlot1()
{
    HandleReadyClicked(1);
}

void ULobbySessionWidget::HandleReadyClickedSlot2()
{
    HandleReadyClicked(2);
}

void ULobbySessionWidget::HandleReadyClickedSlot3()
{
    HandleReadyClicked(3);
}

void ULobbySessionWidget::HandleFactionSelectedSlot0(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    HandleFactionSelected(0, SelectedItem, SelectionType);
}

void ULobbySessionWidget::HandleFactionSelectedSlot1(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    HandleFactionSelected(1, SelectedItem, SelectionType);
}

void ULobbySessionWidget::HandleFactionSelectedSlot2(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    HandleFactionSelected(2, SelectedItem, SelectionType);
}

void ULobbySessionWidget::HandleFactionSelectedSlot3(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    HandleFactionSelected(3, SelectedItem, SelectionType);
}

void ULobbySessionWidget::HandleNameCommittedSlot0(const FText& Text, ETextCommit::Type CommitType)
{
    HandleNameCommitted(0, Text, CommitType);
}

void ULobbySessionWidget::HandleNameCommittedSlot1(const FText& Text, ETextCommit::Type CommitType)
{
    HandleNameCommitted(1, Text, CommitType);
}

void ULobbySessionWidget::HandleNameCommittedSlot2(const FText& Text, ETextCommit::Type CommitType)
{
    HandleNameCommitted(2, Text, CommitType);
}

void ULobbySessionWidget::HandleNameCommittedSlot3(const FText& Text, ETextCommit::Type CommitType)
{
    HandleNameCommitted(3, Text, CommitType);
}

