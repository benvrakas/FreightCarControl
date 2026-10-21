#include "UI/FCCStopRulePanel.h"
#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "UI/FCCCarModeButton.h"
#include "UI/FCCTimeTableEditSession.h"
#include "UI/FCCWidgetUtils.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "FGFreightWagon.h"
#include "FGRailroadSubsystem.h"
#include "FGTrain.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "FreightCarControl"

namespace
{
	constexpr float StopPanelPollInterval = 0.25f;

	// Returns the freight cars of Train in order.
	TArray<const AFGFreightWagon*> GetFreightCars(const AFGTrain* Train)
	{
		TArray<const AFGFreightWagon*> Cars;
		if (Train)
		{
			for (AFGRailroadSubsystem::TConstTrainIterator It(Train->GetFirstVehicle()); It; ++It)
			{
				if (const AFGFreightWagon* Wagon = Cast<AFGFreightWagon>(*It))
				{
					Cars.Add(Wagon);
				}
			}
		}
		return Cars;
	}
}

TSharedRef<SWidget> UFCCStopRulePanel::RebuildWidget()
{
	BuildTree();
	return Super::RebuildWidget();
}

void UFCCStopRulePanel::BuildTree()
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

	const FText Title = LOCTEXT("StopPanelTitle", "Freight cars at this stop");
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

	mCars = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass());
	mCars->SetInnerSlotPadding(FVector2D(6.f, 6.f));
	mRoot->AddChildToVerticalBox(mCars);
}

void UFCCStopRulePanel::NativeConstruct()
{
	Super::NativeConstruct();
	UUserWidget* Settings = FCCWidgetUtils::GetOwningUserWidget(this);
	FCCWidgetUtils::CopyTextStyle(mHint, Settings, TEXT("mLoadText"));
	if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(Slot))
	{
		VerticalSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
		VerticalSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	if (UButton* Discard = FCCWidgetUtils::GetInnerButton(Cast<UUserWidget>(FCCWidgetUtils::GetObjectProperty(Settings, TEXT("mDiscard")))))
	{
		Discard->OnClicked.AddUniqueDynamic(this, &UFCCStopRulePanel::HandleDiscard);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(mPollTimer, this, &UFCCStopRulePanel::Poll, StopPanelPollInterval, true);
	}
	mStationWidget.Reset();
	mCarCount = INDEX_NONE;
	Poll();
	UE_LOG(LogFreightCarControl, Log, TEXT("Stop settings panel shown in %s"), *GetNameSafe(Settings));
}

void UFCCStopRulePanel::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(mPollTimer);
	}
	Super::NativeDestruct();
}

void UFCCStopRulePanel::Poll()
{
	UUserWidget* Settings = FCCWidgetUtils::GetOwningUserWidget(this);
	UUserWidget* StationWidget = Cast<UUserWidget>(FCCWidgetUtils::GetObjectProperty(Settings, TEXT("mStationWidget")));
	UFCCTimeTableEditSession* Session = UFCCTimeTableEditSession::GetActive();
	AFGTrain* Train = Session ? Session->GetTrain() : nullptr;
	const bool bSession = Session && Train && StationWidget;
	const int32 CarCount = AFCCRuleSubsystem::GetFreightCarCount(Train);

	if (bSession == bHasSession && StationWidget == mStationWidget.Get() && CarCount == mCarCount)
	{
		return;
	}
	const bool bNewStop = StationWidget != mStationWidget.Get();
	bHasSession = bSession;
	mStationWidget = StationWidget;
	mCarCount = CarCount;

	if (!bSession)
	{
		mHint->SetText(LOCTEXT("StopPanelNoSession", "Freight car rules are set from a locomotive's timetable editor."));
		RebuildButtons(0);
		UE_LOG(LogFreightCarControl, Verbose, TEXT("Stop settings panel: no editor session (session %s, train %s, stop widget %s)"),
			*GetNameSafe(Session), *GetNameSafe(Train), *GetNameSafe(StationWidget));
		return;
	}
	if (bNewStop)
	{
		mRule = Session->GetRule(StationWidget);
		mRuleWhenOpened = mRule;
	}
	mHint->SetText(CarCount > 0
		? LOCTEXT("StopPanelHint", "Click a car to change what it does here. Platform: use the platform's own setting. Skip: leave the car alone. Load / Unload: switch the platform for this car only.")
		: LOCTEXT("StopPanelNoCars", "This train has no freight cars."));
	RebuildButtons(CarCount);
	UE_LOG(LogFreightCarControl, Verbose, TEXT("Stop settings panel: %s, %d freight cars"), *GetNameSafe(StationWidget), CarCount);
}

void UFCCStopRulePanel::RebuildButtons(int32 CarCount)
{
	mCars->ClearChildren();
	mButtons.Reset();
	UFCCTimeTableEditSession* Session = UFCCTimeTableEditSession::GetActive();
	const TArray<const AFGFreightWagon*> Cars = GetFreightCars(Session ? Session->GetTrain() : nullptr);
	for (int32 CarIndex = 0; CarIndex < CarCount; ++CarIndex)
	{
		UFCCCarModeButton* Button = CreateWidget<UFCCCarModeButton>(this, UFCCCarModeButton::StaticClass());
		if (!Button)
		{
			continue;
		}
		const bool bFluid = Cars.IsValidIndex(CarIndex) && Cars[CarIndex]->GetFreightCargoType() == EFreightCargoType::FCT_Liquid;
		Button->Index = CarIndex;
		Button->SetLabel(bFluid
			? FText::Format(LOCTEXT("FluidCarLabel", "Car {0} (fluid)"), CarIndex + 1)
			: FText::Format(LOCTEXT("CarLabel", "Car {0}"), CarIndex + 1));
		Button->SetMode(mRule.GetCarMode(CarIndex));
		Button->OnClickedHandler = [this](UFCCCarModeButton* Clicked) { OnCarClicked(Clicked); };
		mCars->AddChildToWrapBox(Button);
		mButtons.Add(Button);
	}
}

void UFCCStopRulePanel::OnCarClicked(UFCCCarModeButton* Button)
{
	UFCCTimeTableEditSession* Session = UFCCTimeTableEditSession::GetActive();
	if (!Button || !Session || !mStationWidget.IsValid())
	{
		return;
	}
	const EFCCCarMode Mode = FCCCarMode::Next(Button->GetMode());
	mRule.SetCarMode(Button->Index, Mode);
	mRule.Trim();
	Button->SetMode(Mode);
	Session->SetRule(mStationWidget.Get(), mRule);
	UE_LOG(LogFreightCarControl, Verbose, TEXT("Stop settings panel: car %d -> %s"), Button->Index + 1, *FCCCarMode::GetDisplayName(Mode).ToString());
}

void UFCCStopRulePanel::HandleDiscard()
{
	UFCCTimeTableEditSession* Session = UFCCTimeTableEditSession::GetActive();
	if (Session && mStationWidget.IsValid())
	{
		Session->SetRule(mStationWidget.Get(), mRuleWhenOpened);
	}
	mRule = mRuleWhenOpened;
	mStationWidget.Reset();
}

#undef LOCTEXT_NAMESPACE
