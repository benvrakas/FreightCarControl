#include "UI/FCCWagonRulePanel.h"
#include "FCCRemoteCallObject.h"
#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "UI/FCCCarModeButton.h"
#include "UI/FCCWidgetUtils.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
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

	const FText Title = LOCTEXT("WagonPanelTitle", "Timetable rules for this car");
	if (UUserWidget* Label = FCCWidgetUtils::CreateMenuLabel(WidgetTree, Title))
	{
		mRoot->AddChildToVerticalBox(Label)->SetPadding(FMargin(0.f, 12.f, 0.f, 4.f));
	}
	else
	{
		UTextBlock* TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		TitleText->SetText(Title);
		mRoot->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.f, 12.f, 0.f, 4.f));
	}
	mHint = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	mHint->SetAutoWrapText(true);
	mRoot->AddChildToVerticalBox(mHint)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	mRows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	mRoot->AddChildToVerticalBox(mRows);
}

void UFCCWagonRulePanel::NativeConstruct()
{
	Super::NativeConstruct();
	UUserWidget* Menu = FCCWidgetUtils::GetOwningUserWidget(this);
	FCCWidgetUtils::CopyTextStyle(mHint, Menu);
	if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(Slot))
	{
		VerticalSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
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
	Poll();
	UE_LOG(LogFreightCarControl, Log, TEXT("Freight car panel shown in %s for %s"), *GetNameSafe(Menu), *GetNameSafe(GetWagon()));
}

void UFCCWagonRulePanel::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(mPollTimer);
	}
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
	if (!bDirty && Stations == mShownStations && CarIndex == mShownCarIndex)
	{
		return;
	}
	bDirty = false;
	mShownStations = Stations;
	mShownCarIndex = CarIndex;
	RebuildRows();
}

void UFCCWagonRulePanel::RebuildRows()
{
	mRows->ClearChildren();
	mButtons.Reset();
	AFGFreightWagon* Wagon = GetWagon();
	AFGTrain* Train = Wagon ? Wagon->GetTrain() : nullptr;
	AFGRailroadTimeTable* TimeTable = Train ? Train->GetTimeTable() : nullptr;
	const AFCCRuleSubsystem* Subsystem = AFCCRuleSubsystem::Get(this);
	if (!TimeTable || TimeTable->GetNumStops() == 0 || mShownCarIndex == INDEX_NONE || !Subsystem)
	{
		mHint->SetText(LOCTEXT("WagonPanelNoTimeTable", "This car's train has no timetable stops."));
		return;
	}
	mHint->SetText(FText::Format(LOCTEXT("WagonPanelHint", "Car {0} of {1}. Click a stop to change what this car does there."),
		mShownCarIndex + 1, AFCCRuleSubsystem::GetFreightCarCount(Train)));
	for (int32 StopIndex = 0; StopIndex < TimeTable->GetNumStops(); ++StopIndex)
	{
		const FTimeTableStop Stop = TimeTable->GetStop(StopIndex);
		UFCCCarModeButton* Button = CreateWidget<UFCCCarModeButton>(this, UFCCCarModeButton::StaticClass());
		if (!Button)
		{
			continue;
		}
		Button->Index = StopIndex;
		Button->SetLabel(FText::Format(LOCTEXT("StopLabel", "{0}. {1}"), StopIndex + 1,
			Stop.Station ? Stop.Station->GetStationName() : LOCTEXT("UnknownStation", "?")));
		Button->SetMode(Subsystem->GetCarMode(TimeTable, StopIndex, mShownCarIndex));
		Button->OnClickedHandler = [this](UFCCCarModeButton* Clicked) { OnStopClicked(Clicked); };
		mRows->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
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
