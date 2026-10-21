#include "UI/FCCWagonRulePanel.h"
#include "FCCRemoteCallObject.h"
#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "UI/FCCCarModeButton.h"
#include "UI/FCCWidgetUtils.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "FGFreightWagon.h"
#include "FGRailroadTimeTable.h"
#include "FGTrain.h"
#include "FGTrainStationIdentifier.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "FreightCarControl"

namespace
{
	constexpr float WagonPollInterval = 0.5f;

	// Tallest the expanded stop list gets before it scrolls.
	constexpr float MaxListHeight = 160.f;

	// Whether the stop list is open; kept across menus so it stays as the player left it.
	bool GWagonPanelExpanded = false;

	// Extra window size while the stop list is closed (room for the toggle button) and open (room for the list).
	const FVector2D ExtraSizeClosed(0.f, 60.f);
	const FVector2D ExtraSizeOpen(280.f, 60.f + MaxListHeight + 10.f);

	// Size of the vanilla fluid car layout, which has no size override of its own (its background image's size).
	const FVector2D FluidLayoutSize(611.f, 486.f);
}

void UFCCWagonRulePanel::FWindowLayout::Capture(UUserWidget* Menu, bool bFluid)
{
	if (bCaptured || !Menu || !Menu->WidgetTree)
	{
		return;
	}
	Container = Cast<USizeBox>(Menu->WidgetTree->FindWidget(bFluid ? TEXT("mLiquidInventoryContainer") : TEXT("mSolidInventoryContainer")));
	const UWidget* Column = bFluid ? nullptr : Menu->WidgetTree->FindWidget(TEXT("VerticalBox_0"));
	ColumnSlot = Column ? Cast<UCanvasPanelSlot>(Column->Slot) : nullptr;
	if (!Container.IsValid() || (!bFluid && !ColumnSlot.IsValid()))
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("Freight car panel: window parts not found (container %s, column %s); window not resized"),
			*GetNameSafe(Container.Get()), *GetNameSafe(ColumnSlot.Get()));
		return;
	}
	Size = bFluid ? FluidLayoutSize : FVector2D(Container->GetWidthOverride(), Container->GetHeightOverride());
	MaxWidth = Container->GetMaxDesiredWidth();
	ColumnOffsets = ColumnSlot.IsValid() ? ColumnSlot->GetOffsets() : FMargin();
	bCaptured = true;
}

void UFCCWagonRulePanel::FWindowLayout::Apply(const FVector2D& Extra) const
{
	if (!bCaptured || !Container.IsValid())
	{
		return;
	}
	Container->SetWidthOverride(Size.X + Extra.X);
	Container->SetHeightOverride(Size.Y + Extra.Y);
	if (MaxWidth > 0.f)
	{
		Container->SetMaxDesiredWidth(MaxWidth + Extra.X);
	}
	if (ColumnSlot.IsValid())
	{
		// The column is centred vertically on its Top offset; moving the centre by half keeps its top edge in place.
		FMargin Offsets = ColumnOffsets;
		Offsets.Right += Extra.X;
		Offsets.Bottom += Extra.Y;
		Offsets.Top += Extra.Y * 0.5f;
		ColumnSlot->SetOffsets(Offsets);
	}
}

void UFCCWagonRulePanel::FWindowLayout::Restore()
{
	Apply(FVector2D::ZeroVector);
	bCaptured = false;
}

TSharedRef<SWidget> UFCCWagonRulePanel::RebuildWidget()
{
	BuildTree();
	return Super::RebuildWidget();
}

void UFCCWagonRulePanel::BuildTree()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (WidgetTree->RootWidget)
	{
		return;
	}
	mRoot = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	WidgetTree->RootWidget = mRoot;

	mToggle = FCCWidgetUtils::CreateStandardButton(WidgetTree, LOCTEXT("WagonPanelToggle", "Timetable rules"));
	if (mToggle)
	{
		mRoot->AddChildToVerticalBox(mToggle)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	}

	mListBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	mListBox->SetMaxDesiredHeight(MaxListHeight);
	mRows = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	mListBox->AddChild(mRows);
	mRoot->AddChildToVerticalBox(mListBox)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
}

void UFCCWagonRulePanel::NativeConstruct()
{
	Super::NativeConstruct();
	bActive = IsForCargoType(GetWagon());
	SetVisibility(bActive ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (!bActive)
	{
		return;
	}
	if (UButton* Inner = FCCWidgetUtils::GetInnerButton(mToggle))
	{
		Inner->OnClicked.AddUniqueDynamic(this, &UFCCWagonRulePanel::HandleToggle);
	}
	else
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("Freight car panel: toggle button has no mButton; list stays open"));
		GWagonPanelExpanded = true;
	}
	bDirty = true;
	Poll();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(mPollTimer, this, &UFCCWagonRulePanel::Poll, WagonPollInterval, true);
		// The window's own layout is still being built during construction; resize it once that is done.
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (bActive)
			{
				mLayout.Capture(FCCWidgetUtils::GetOwningUserWidget(this), Cast<UCanvasPanelSlot>(Slot) != nullptr);
				RefreshExpanded();
			}
		}));
	}
	UE_LOG(LogFreightCarControl, Log, TEXT("Freight car panel shown for %s"), *GetNameSafe(GetWagon()));
}

void UFCCWagonRulePanel::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(mPollTimer);
	}
	// The game reuses menu widgets; leave the window as it was found.
	bActive = false;
	mLayout.Restore();
	Super::NativeDestruct();
}

AFGFreightWagon* UFCCWagonRulePanel::GetWagon() const
{
	return Cast<AFGFreightWagon>(FCCWidgetUtils::GetObjectProperty(FCCWidgetUtils::GetOwningUserWidget(this), TEXT("mInteractObject")));
}

bool UFCCWagonRulePanel::IsForCargoType(const AFGFreightWagon* Wagon) const
{
	const bool bFluidLayout = Cast<UCanvasPanelSlot>(Slot) != nullptr;
	return Wagon && bFluidLayout == (Wagon->GetFreightCargoType() == EFreightCargoType::FCT_Liquid);
}

void UFCCWagonRulePanel::HandleToggle()
{
	GWagonPanelExpanded = !GWagonPanelExpanded;
	RefreshExpanded();
}

void UFCCWagonRulePanel::RefreshExpanded()
{
	const int32 StopCount = mShownStations.Num();
	const bool bOpen = GWagonPanelExpanded && StopCount > 0;
	FText Text = LOCTEXT("WagonToggleNoStops", "Timetable rules: no timetable");
	if (mShownCarIndex != INDEX_NONE && StopCount > 0)
	{
		Text = FText::Format(bOpen
				? LOCTEXT("WagonToggleOpen", "Hide timetable rules (car {0} of {1})")
				: LOCTEXT("WagonToggleClosed", "Timetable rules (car {0} of {1}, {2} stops)"),
			mShownCarIndex + 1, mShownCarCount, StopCount);
	}
	FCCWidgetUtils::SetWidgetText(mToggle, Text);
	mListBox->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	mLayout.Apply(bOpen ? ExtraSizeOpen : ExtraSizeClosed);
}

void UFCCWagonRulePanel::Poll()
{
	const AFGFreightWagon* Wagon = GetWagon();
	const AFGTrain* Train = Wagon ? Wagon->GetTrain() : nullptr;
	const AFGRailroadTimeTable* TimeTable = Train ? Train->GetTimeTable() : nullptr;
	const AFCCRuleSubsystem* Subsystem = AFCCRuleSubsystem::Get(this);
	TArray<TWeakObjectPtr<UObject>> Stations;
	for (int32 StopIndex = 0; TimeTable && StopIndex < TimeTable->GetNumStops(); ++StopIndex)
	{
		Stations.Add(TimeTable->GetStop(StopIndex).Station);
	}
	const TArray<AFGFreightWagon*> Cars = AFCCRuleSubsystem::GetFreightCars(Train);
	const int32 CarIndex = Cars.IndexOfByKey(Wagon);
	const uint32 Revision = Subsystem ? Subsystem->GetRevision() : 0;
	if (!bDirty && Stations == mShownStations && CarIndex == mShownCarIndex && Cars.Num() == mShownCarCount && Revision == mShownRevision)
	{
		return;
	}
	bDirty = false;
	mShownStations = Stations;
	mShownCarIndex = CarIndex;
	mShownCarCount = Cars.Num();
	mShownRevision = Revision;
	RebuildRows();
	RefreshExpanded();
}

void UFCCWagonRulePanel::RebuildRows()
{
	mRows->ClearChildren();
	const AFGFreightWagon* Wagon = GetWagon();
	const AFGTrain* Train = Wagon ? Wagon->GetTrain() : nullptr;
	const AFGRailroadTimeTable* TimeTable = Train ? Train->GetTimeTable() : nullptr;
	const AFCCRuleSubsystem* Subsystem = AFCCRuleSubsystem::Get(this);
	if (!TimeTable || mShownCarIndex == INDEX_NONE || !Subsystem)
	{
		return;
	}
	for (int32 StopIndex = 0; StopIndex < TimeTable->GetNumStops(); ++StopIndex)
	{
		UFCCCarModeButton* Button = CreateWidget<UFCCCarModeButton>(this, UFCCCarModeButton::StaticClass());
		if (!Button)
		{
			continue;
		}
		const AFGTrainStationIdentifier* Station = TimeTable->GetStop(StopIndex).Station;
		Button->Index = StopIndex;
		Button->bModeFirst = true;
		Button->SetLabel(FText::Format(LOCTEXT("StopLabel", "{0}. {1}"), StopIndex + 1, Station ? Station->GetStationName() : LOCTEXT("UnknownStation", "?")));
		Button->SetMode(Subsystem->GetCarMode(TimeTable, StopIndex, mShownCarIndex));
		Button->OnClickedHandler = [this](UFCCCarModeButton* Clicked) { OnStopClicked(Clicked); };
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(mRows->AddChild(Button)))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		}
	}
}

void UFCCWagonRulePanel::OnStopClicked(UFCCCarModeButton* Button)
{
	const AFGFreightWagon* Wagon = GetWagon();
	const AFGTrain* Train = Wagon ? Wagon->GetTrain() : nullptr;
	AFGRailroadTimeTable* TimeTable = Train ? Train->GetTimeTable() : nullptr;
	const AFCCRuleSubsystem* Subsystem = AFCCRuleSubsystem::Get(this);
	UFCCRemoteCallObject* RCO = UFCCRemoteCallObject::GetLocal(this);
	if (!Button || !TimeTable || !Subsystem || !RCO || mShownCarIndex == INDEX_NONE)
	{
		return;
	}
	const EFCCCarMode Mode = FCCCarMode::Next(Button->GetMode());
	FFCCStopRule Rule = Subsystem->GetStopRule(TimeTable, Button->Index);
	Rule.SetCarMode(mShownCarIndex, Mode);
	Rule.Trim();
	Button->SetMode(Mode);
	RCO->Server_SetStopRule(TimeTable, Button->Index, Rule);
}

#undef LOCTEXT_NAMESPACE
