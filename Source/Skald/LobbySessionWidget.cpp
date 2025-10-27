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

    RootPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RootPanel"));
    WidgetTree->RootWidget = RootPanel;

    SlotsPanel = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SlotsPanel"));
    RootPanel->AddChild(SlotsPanel);

    for (int32 Index = 0; Index < SlotWidgets.Num(); ++Index)
    {
        FLobbySlotWidgets& SlotWidget = SlotWidgets[Index];

        UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
        Border->SetPadding(FMargin(8.f));
        Border->SetBrushColor(InactiveSlotColor);
        SlotWidget.Container = Border;

        UVerticalBox* SlotBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
        Border->SetContent(SlotBox);

        UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Title->SetText(FText::Format(NSLOCTEXT("Lobby", "SlotTitle", "Player {0}"), FText::AsNumber(Index + 1)));
        Title->SetJustification(ETextJustify::Center);
        SlotWidget.Title = Title;
        SlotBox->AddChildToVerticalBox(Title);

        UEditableTextBox* NameEdit = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
        NameEdit->SetHintText(NSLOCTEXT("Lobby", "NameHint", "Display Name"));
        switch (Index)
        {
        case 0:
            NameEdit->OnTextCommitted.AddDynamic(this, &ULobbySessionWidget::HandleNameCommittedSlot0);
            break;
        case 1:
            NameEdit->OnTextCommitted.AddDynamic(this, &ULobbySessionWidget::HandleNameCommittedSlot1);
            break;
        case 2:
            NameEdit->OnTextCommitted.AddDynamic(this, &ULobbySessionWidget::HandleNameCommittedSlot2);
            break;
        case 3:
            NameEdit->OnTextCommitted.AddDynamic(this, &ULobbySessionWidget::HandleNameCommittedSlot3);
            break;
        default:
            break;
        }
        SlotWidget.NameEdit = NameEdit;
        SlotBox->AddChildToVerticalBox(NameEdit);

        UComboBoxString* FactionCombo = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass());
        RebuildFactionOptions(FactionCombo);
        FactionCombo->SetSelectedOption(FactionPlaceholder);
        switch (Index)
        {
        case 0:
            FactionCombo->OnSelectionChanged.AddDynamic(this, &ULobbySessionWidget::HandleFactionSelectedSlot0);
            break;
        case 1:
            FactionCombo->OnSelectionChanged.AddDynamic(this, &ULobbySessionWidget::HandleFactionSelectedSlot1);
            break;
        case 2:
            FactionCombo->OnSelectionChanged.AddDynamic(this, &ULobbySessionWidget::HandleFactionSelectedSlot2);
            break;
        case 3:
            FactionCombo->OnSelectionChanged.AddDynamic(this, &ULobbySessionWidget::HandleFactionSelectedSlot3);
            break;
        default:
            break;
        }
        SlotWidget.FactionCombo = FactionCombo;
        SlotBox->AddChildToVerticalBox(FactionCombo);

        UHorizontalBox* ReadyRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        SlotBox->AddChildToVerticalBox(ReadyRow);

        UButton* ReadyButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
        switch (Index)
        {
        case 0:
            ReadyButton->OnClicked.AddDynamic(this, &ULobbySessionWidget::HandleReadyClickedSlot0);
            break;
        case 1:
            ReadyButton->OnClicked.AddDynamic(this, &ULobbySessionWidget::HandleReadyClickedSlot1);
            break;
        case 2:
            ReadyButton->OnClicked.AddDynamic(this, &ULobbySessionWidget::HandleReadyClickedSlot2);
            break;
        case 3:
            ReadyButton->OnClicked.AddDynamic(this, &ULobbySessionWidget::HandleReadyClickedSlot3);
            break;
        default:
            break;
        }
        SlotWidget.ReadyButton = ReadyButton;

        UTextBlock* ReadyLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        ReadyLabel->SetText(NSLOCTEXT("Lobby", "Ready", "Ready"));
        SlotWidget.ReadyLabel = ReadyLabel;
        ReadyButton->SetContent(ReadyLabel);

        UHorizontalBoxSlot* ReadyButtonSlot = ReadyRow->AddChildToHorizontalBox(ReadyButton);
        ReadyButtonSlot->SetPadding(FMargin(0.f, 4.f, 12.f, 0.f));

        UTextBlock* StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        StatusText->SetText(NSLOCTEXT("Lobby", "StatusNotReady", "Not Ready"));
        SlotWidget.StatusText = StatusText;
        ReadyRow->AddChildToHorizontalBox(StatusText);

        UHorizontalBoxSlot* SlotSlot = SlotsPanel->AddChildToHorizontalBox(Border);
        SlotSlot->SetPadding(FMargin(4.f));
        SlotSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
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
    PlayerCountSpinBox->OnValueCommitted.AddDynamic(this, &ULobbySessionWidget::HandlePlayerCountCommitted);
    HostControls->AddChildToHorizontalBox(PlayerCountSpinBox);

    AddLabel(HostControls, NSLOCTEXT("Lobby", "AICount", "AI:"));

    AICountSpinBox = WidgetTree->ConstructWidget<USpinBox>(USpinBox::StaticClass());
    AICountSpinBox->SetMinValue(0.f);
    AICountSpinBox->SetMaxValue(3.f);
    AICountSpinBox->SetDelta(1.f);
    AICountSpinBox->OnValueCommitted.AddDynamic(this, &ULobbySessionWidget::HandleAICountCommitted);
    HostControls->AddChildToHorizontalBox(AICountSpinBox);

    LaunchButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
    LaunchButton->OnClicked.AddDynamic(this, &ULobbySessionWidget::HandleLaunchClicked);
    LaunchLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    LaunchLabel->SetText(NSLOCTEXT("Lobby", "Launch", "Launch Game"));
    LaunchButton->SetContent(LaunchLabel);
    HostControls->AddChildToHorizontalBox(LaunchButton);
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
        for (int32 Index = 0; Index < SlotWidgets.Num(); ++Index)
        {
            const FLobbyPlayerSlot* Slot = CachedGameState->GetSlot(Index);
            if (Slot)
            {
                RefreshSlot(Index, *Slot, LocalSlotIndex);
            }
        }

        UpdateHostControls(CachedGameState->TotalSlots, CachedGameState->ReservedAISlots, CachedGameState->AreAllSlotsReady());
    }

    bIsUpdatingFromState = false;
}

void ULobbySessionWidget::RefreshSlot(int32 SlotIndex, const FLobbyPlayerSlot& Slot, int32 LocalSlotIndex)
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

    Widgets.Container->SetBrushColor(Slot.bIsActive ? ActiveSlotColor : InactiveSlotColor);

    const bool bIsLocal = SlotIndex == LocalSlotIndex;
    const bool bIsAI = Slot.bIsAI;

    if (Widgets.NameEdit)
    {
        Widgets.NameEdit->SetIsEnabled(Slot.bIsActive && !bIsAI && bIsLocal);
        Widgets.NameEdit->SetText(FText::FromString(Slot.DisplayName));
    }

    if (Widgets.FactionCombo)
    {
        Widgets.FactionCombo->ClearSelection();
        if (Slot.Faction != ESkaldFaction::None)
        {
            if (UEnum* Enum = StaticEnum<ESkaldFaction>())
            {
                const FString Name = Enum->GetNameStringByValue(static_cast<int64>(Slot.Faction));
                Widgets.FactionCombo->SetSelectedOption(Name);
            }
        }
        else
        {
            Widgets.FactionCombo->SetSelectedOption(FactionPlaceholder);
        }

        Widgets.FactionCombo->SetIsEnabled(Slot.bIsActive && !bIsAI && bIsLocal);
    }

    if (Widgets.ReadyButton && Widgets.ReadyLabel)
    {
        Widgets.ReadyButton->SetVisibility(Slot.bIsActive && !bIsAI ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        Widgets.ReadyButton->SetIsEnabled(bIsLocal && !bIsAI);
        Widgets.ReadyLabel->SetText(Slot.bIsReady ? NSLOCTEXT("Lobby", "Unready", "Unready") : NSLOCTEXT("Lobby", "ReadyAction", "Ready"));
    }

    if (Widgets.StatusText)
    {
        if (!Slot.bIsActive)
        {
            Widgets.StatusText->SetText(NSLOCTEXT("Lobby", "Inactive", "Inactive"));
            Widgets.StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::Gray));
        }
        else if (bIsAI)
        {
            Widgets.StatusText->SetText(NSLOCTEXT("Lobby", "AIReady", "AI Ready"));
            Widgets.StatusText->SetColorAndOpacity(FSlateColor(ReadyColor));
        }
        else
        {
            Widgets.StatusText->SetText(Slot.bIsReady ? NSLOCTEXT("Lobby", "ReadyStatus", "Ready") : NSLOCTEXT("Lobby", "NotReadyStatus", "Not Ready"));
            Widgets.StatusText->SetColorAndOpacity(FSlateColor(Slot.bIsReady ? ReadyColor : NotReadyColor));
        }
    }
}

void ULobbySessionWidget::UpdateHostControls(int32 TotalSlots, int32 AISlots, bool bAllReady)
{
    const bool bIsHost = CachedController && CachedController->HasAuthority();

    if (PlayerCountSpinBox)
    {
        PlayerCountSpinBox->SetValue(static_cast<float>(TotalSlots));
        PlayerCountSpinBox->SetIsEnabled(bIsHost);
    }

    if (AICountSpinBox)
    {
        AICountSpinBox->SetMaxValue(static_cast<float>(FMath::Max(0, TotalSlots - 1)));
        AICountSpinBox->SetValue(static_cast<float>(AISlots));
        AICountSpinBox->SetIsEnabled(bIsHost);
    }

    if (LaunchButton)
    {
        LaunchButton->SetIsEnabled(bIsHost && bAllReady);
        LaunchButton->SetVisibility(bIsHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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
    if (!bIsUpdatingFromState && CachedController)
    {
        CachedController->RequestPlayerCount(FMath::RoundToInt(Value));
    }
}

void ULobbySessionWidget::HandleAICountCommitted(float Value, ETextCommit::Type CommitType)
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

    if (const FLobbyPlayerSlot* Slot = CachedGameState->GetSlot(SlotIndex))
    {
        CachedController->ToggleReadyState(!Slot->bIsReady);
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

