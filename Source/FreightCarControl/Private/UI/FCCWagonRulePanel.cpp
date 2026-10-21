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

	// Tallest the expanded stop list gets before it scrolls, so the inventory keeps its room.
	constexpr float MaxListHeight = 160.f;

	// Whether the stop list is open; kept across menus so it stays as the player left it.
	bool GWagonPanelExpanded = false;

	// Extra window size while the stop list is closed (room for the toggle button) and open (room for the list).
	const FVector2D ExtraSizeClosed(0.f, 60.f);
	const FVector2D ExtraSizeOpen(280.f, 60.f + MaxListHeight + 10.f);

	// Size of the vanilla fluid car layout, which has no size override of its own (its background image's size).
	const FVector2D FluidLayoutSize(611.f, 486.f);
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
	if (UButton* Inner = FCCWidgetUtils::GetInnerButton(mToggle))
	{
		Inner->OnClicked.AddUniqueDynamic(this, &UFCCWagonRulePanel::HandleToggle);
	}
	else
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("Freight car panel: toggle button has no mButton; list stays open"));
		GWagonPanelExpanded = true;
	}
	if (AFCCRuleSubsystem* Subsystem = AFCCRuleSubsystem::Get(this))
	{
		Subsystem->OnRulesChanged.AddUniqueDynamic(this, &UFCCWagonRulePanel::HandleRulesChanged);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(mPollTimer, this, &UFCCWagonRulePanel::Poll, WagonPollInterval, true);
	}
	bDirty = true;
	bLayoutCaptured = false;
	Poll();
	RefreshExpanded();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			CaptureVanillaLayout();
			ApplyWindowLayout();
		}));
	}
	UE_LOG(LogFreightCarControl, Log, TEXT("Freight car panel shown in %s for %s"), *GetNameSafe(FCCWidgetUtils::GetOwningUserWidget(this)), *GetNameSafe(GetWagon()));
}

void UFCCWagonRulePanel::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(mPollTimer);
	}
	bLayoutCaptured = false;
	if (AFCCRuleSubsystem* Subsystem = AFCCRuleSubsystem::Get(this))
	{
		Subsystem->OnRulesChanged.RemoveDynamic(this, &UFCCWagonRulePanel::HandleRulesChanged);
	}
	Super::NativeDestruct();
}

AFGFreightWagon* UFCCWagonRulePanel::GetWagon() const
{
	return Cast<AFGFreightWagon>(FCCWidgetUtils::GetObjectProperty(FCCWidgetUtils::GetOwningUserWidget(this), TEXT("mInteractObject")));
}

void UFCCWagonRulePanel::HandleToggle()
{
	GWagonPanelExpanded = !GWagonPanelExpanded;
	RefreshExpanded();
}

void UFCCWagonRulePanel::RefreshExpanded()
{
	const int32 StopCount = mShownStations.Num();
	FText Text;
	if (mShownCarIndex == INDEX_NONE || StopCount == 0)
	{
		Text = LOCTEXT("WagonToggleNoStops", "Timetable rules: no timetable");
	}
	else
	{
		Text = FText::Format(GWagonPanelExpanded
				? LOCTEXT("WagonToggleOpen", "Hide timetable rules (car {0} of {1})")
				: LOCTEXT("WagonToggleClosed", "Timetable rules (car {0} of {1}, {2} stops)"),
			mShownCarIndex + 1, mShownCarCount, StopCount);
	}
	FCCWidgetUtils::SetWidgetText(mToggle, Text);
	mListBox->SetVisibility(GWagonPanelExpanded && StopCount > 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	ApplyWindowLayout();
}

void UFCCWagonRulePanel::CaptureVanillaLayout()
{
	UUserWidget* Menu = FCCWidgetUtils::GetOwningUserWidget(this);
	if (!Menu || !Menu->WidgetTree)
	{
		return;
	}
	bFluidLayout = Cast<UCanvasPanelSlot>(Slot) != nullptr;
	mContainer = Cast<USizeBox>(Menu->WidgetTree->FindWidget(bFluidLayout ? TEXT("mLiquidInventoryContainer") : TEXT("mSolidInventoryContainer")));
	UWidget* Column = bFluidLayout ? nullptr : Menu->WidgetTree->FindWidget(TEXT("VerticalBox_0"));
	mColumnSlot = Column ? Cast<UCanvasPanelSlot>(Column->Slot) : nullptr;
	if (!mContainer.IsValid() || (!bFluidLayout && !mColumnSlot.IsValid()))
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("Freight car panel: window parts not found (container %s, column %s); window not resized"),
			*GetNameSafe(mContainer.Get()), *GetNameSafe(mColumnSlot.Get()));
		return;
	}
	bContainerHadOverride = !bFluidLayout;
	mContainerSize = bFluidLayout ? FluidLayoutSize : FVector2D(mContainer->GetWidthOverride(), mContainer->GetHeightOverride());
	mContainerMaxWidth = mContainer->GetMaxDesiredWidth();
	if (mColumnSlot.IsValid())
	{
		mColumnOffsets = mColumnSlot->GetOffsets();
	}
	bLayoutCaptured = true;
	UE_LOG(LogFreightCarControl, Verbose, TEXT("Freight car panel: %s layout %.0fx%.0f captured"), bFluidLayout ? TEXT("fluid") : TEXT("solid"), mContainerSize.X, mContainerSize.Y);
}

void UFCCWagonRulePanel::ApplyWindowLayout()
{
	if (!bLayoutCaptured || !mContainer.IsValid())
	{
		return;
	}
	const FVector2D Extra = GWagonPanelExpanded && mShownStations.Num() > 0 ? ExtraSizeOpen : ExtraSizeClosed;
	mContainer->SetWidthOverride(mContainerSize.X + Extra.X);
	mContainer->SetHeightOverride(mContainerSize.Y + Extra.Y);
	if (mContainerMaxWidth > 0.f)
	{
		mContainer->SetMaxDesiredWidth(mContainerMaxWidth + Extra.X);
	}
	if (mColumnSlot.IsValid())
	{
		FMargin Offsets = mColumnOffsets;
		Offsets.Right += Extra.X;
		Offsets.Bottom += Extra.Y;
		Offsets.Top += Extra.Y * 0.5f;
		mColumnSlot->SetOffsets(Offsets);
	}
}

void UFCCWagonRulePanel::HandleRulesChanged()
{
	bDirty = true;
	Poll();
}

void UFCCWagonRulePanel::Poll()
{
	AFGFreightWagon* Wagon = GetWagon();
	AFGTrain* Train = Wagon ? Wagon->GetTrain() : nullptr;
	AFGRailroadTimeTable* TimeTable = Train ? Train->GetTimeTable() : nullptr;
	TArray<TWeakObjectPtr<UObject>> Stations;
	if (TimeTable)
	{
		TArray<FTimeTableStop> Stops;
		TimeTable->GetStops(Stops);
		for (const FTimeTableStop& Stop : Stops)
		{
			Stations.Add(Stop.Station);
		}
	}
	const int32 CarIndex = AFCCRuleSubsystem::GetFreightCarIndex(Wagon);
	const int32 CarCount = AFCCRuleSubsystem::GetFreightCarCount(Train);
	if (!bDirty && Stations == mShownStations && CarIndex == mShownCarIndex && CarCount == mShownCarCount)
	{
		return;
	}
	bDirty = false;
	mShownStations = Stations;
	mShownCarIndex = CarIndex;
	mShownCarCount = CarCount;
	RebuildRows();
	RefreshExpanded();
}

void UFCCWagonRulePanel::RebuildRows()
{
	mRows->ClearChildren();
	mButtons.Reset();
	AFGFreightWagon* Wagon = GetWagon();
	AFGTrain* Train = Wagon ? Wagon->GetTrain() : nullptr;
	AFGRailroadTimeTable* TimeTable = Train ? Train->GetTimeTable() : nullptr;
	const AFCCRuleSubsystem* Subsystem = AFCCRuleSubsystem::Get(this);
	if (!TimeTable || mShownCarIndex == INDEX_NONE || !Subsystem)
	{
		return;
	}
	for (int32 StopIndex = 0; StopIndex < TimeTable->GetNumStops(); ++StopIndex)
	{
		const FTimeTableStop Stop = TimeTable->GetStop(StopIndex);
		UFCCCarModeButton* Button = CreateWidget<UFCCCarModeButton>(this, UFCCCarModeButton::StaticClass());
		if (!Button)
		{
			continue;
		}
		Button->Index = StopIndex;
		Button->bModeFirst = true;
		Button->SetLabel(FText::Format(LOCTEXT("StopLabel", "{0}. {1}"), StopIndex + 1,
			Stop.Station ? Stop.Station->GetStationName() : LOCTEXT("UnknownStation", "?")));
		Button->SetMode(Subsystem->GetCarMode(TimeTable, StopIndex, mShownCarIndex));
		Button->OnClickedHandler = [this](UFCCCarModeButton* Clicked) { OnStopClicked(Clicked); };
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(mRows->AddChild(Button)))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		}
		mButtons.Add(Button);
	}
}

void UFCCWagonRulePanel::OnStopClicked(UFCCCarModeButton* Button)
{
	AFGFreightWagon* Wagon = GetWagon();
	AFGTrain* Train = Wagon ? Wagon->GetTrain() : nullptr;
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
	RCO->Server_SetStopRule(TimeTable, Button->Index, TimeTable->GetStop(Button->Index).Station, Rule);
	UE_LOG(LogFreightCarControl, Verbose, TEXT("Freight car panel: stop %d car %d -> %s"), Button->Index + 1, mShownCarIndex + 1, *FCCCarMode::GetDisplayName(Mode).ToString());
}

#undef LOCTEXT_NAMESPACE
