#include "UI/FCCStopRulePanel.h"
#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "UI/FCCCarModeButton.h"
#include "UI/FCCTimeTableEditSession.h"
#include "UI/FCCWidgetUtils.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "FGFreightWagon.h"
#include "FGTrain.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "FreightCarControl"

namespace
{
	constexpr float StopPanelPollInterval = 0.25f;

	// Width of each car button, so the buttons form even columns.
	constexpr float CarButtonWidth = 200.f;

	// Gap between car buttons.
	constexpr float CarButtonGap = 6.f;

	// Tallest the car grid gets before it scrolls: about three rows of buttons, so long trains fit the window.
	constexpr float MaxGridHeight = 180.f;
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
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	WidgetTree->RootWidget = Root;

	const FText Title = LOCTEXT("StopPanelTitle", "Freight cars at this stop");
	UWidget* Label = FCCWidgetUtils::CreateMenuLabel(WidgetTree, Title);
	if (!Label)
	{
		UTextBlock* TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		TitleText->SetText(Title);
		Label = TitleText;
	}
	Root->AddChildToVerticalBox(Label)->SetPadding(FMargin(0.f, 12.f, 0.f, 4.f));

	mHint = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	mHint->SetAutoWrapText(true);
	Root->AddChildToVerticalBox(mHint)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	USizeBox* GridBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	GridBox->SetMaxDesiredHeight(MaxGridHeight);
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	mCars = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass());
	mCars->SetInnerSlotPadding(FVector2D(CarButtonGap, CarButtonGap));
	Scroll->AddChild(mCars);
	GridBox->AddChild(Scroll);
	Root->AddChildToVerticalBox(GridBox);
}

void UFCCStopRulePanel::NativeConstruct()
{
	Super::NativeConstruct();
	UUserWidget* Settings = FCCWidgetUtils::GetOwningUserWidget(this);
	FCCWidgetUtils::CopyTextStyle(mHint, Settings, TEXT("mLoadText"), 12);
	if (UButton* Discard = FCCWidgetUtils::GetInnerButton(Cast<UUserWidget>(FCCWidgetUtils::GetObjectProperty(Settings, TEXT("mDiscard")))))
	{
		Discard->OnClicked.AddUniqueDynamic(this, &UFCCStopRulePanel::HandleDiscard);
	}
	mStationWidget.Reset();
	mTrain.Reset();
	mCarCount = INDEX_NONE;
	Poll();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(mPollTimer, this, &UFCCStopRulePanel::Poll, StopPanelPollInterval, true);
	}
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
	UUserWidget* StationWidget = Cast<UUserWidget>(FCCWidgetUtils::GetObjectProperty(FCCWidgetUtils::GetOwningUserWidget(this), TEXT("mStationWidget")));
	const UFCCTimeTableEditSession* Session = UFCCTimeTableEditSession::GetActive();
	AFGTrain* Train = Session && StationWidget ? Session->GetTrain() : nullptr;
	const int32 CarCount = AFCCRuleSubsystem::GetFreightCars(Train).Num();
	if (StationWidget == mStationWidget.Get() && Train == mTrain.Get() && CarCount == mCarCount)
	{
		return;
	}
	if (StationWidget != mStationWidget.Get() && Train)
	{
		mRule = Session->GetRule(StationWidget);
		mRuleWhenOpened = mRule;
	}
	mStationWidget = StationWidget;
	mTrain = Train;
	mCarCount = CarCount;

	if (!Train)
	{
		mHint->SetText(LOCTEXT("StopPanelNoSession", "Freight car rules are set from a locomotive's timetable editor."));
	}
	else if (CarCount == 0)
	{
		mHint->SetText(LOCTEXT("StopPanelNoCars", "This train has no freight cars."));
	}
	else
	{
		const FText Numbering = AFCCRuleSubsystem::DescribeCarNumbering(Train);
		mHint->SetText(FText::Format(LOCTEXT("StopPanelHint", "Click a car to change what it does here (right-click to go back). Platform: the platform's own setting. Skip: leave the car alone. Load / Unload: switch the platform for this car only. {0}"), Numbering));
	}
	RebuildButtons(Train);
	UE_LOG(LogFreightCarControl, Verbose, TEXT("Stop settings panel: %s, train %s, %d freight cars"), *GetNameSafe(StationWidget), *GetNameSafe(Train), CarCount);
}

void UFCCStopRulePanel::RebuildButtons(const AFGTrain* Train)
{
	mCars->ClearChildren();
	const TArray<AFGFreightWagon*> Cars = AFCCRuleSubsystem::GetFreightCars(Train);
	for (int32 CarIndex = 0; CarIndex < Cars.Num(); ++CarIndex)
	{
		UFCCCarModeButton* Button = CreateWidget<UFCCCarModeButton>(this, UFCCCarModeButton::StaticClass());
		if (!Button)
		{
			continue;
		}
		const bool bFluid = Cars[CarIndex]->GetFreightCargoType() == EFreightCargoType::FCT_Liquid;
		Button->Index = CarIndex;
		Button->SetFixedWidth(CarButtonWidth);
		Button->SetLabel(FText::Format(bFluid ? LOCTEXT("FluidCarLabel", "Car {0} (fluid)") : LOCTEXT("CarLabel", "Car {0}"), CarIndex + 1));
		Button->SetMode(mRule.GetCarMode(CarIndex));
		Button->OnClickedHandler = [this](UFCCCarModeButton* Clicked, EFCCCarMode Mode) { OnCarClicked(Clicked, Mode); };
		mCars->AddChildToWrapBox(Button);
	}
}

void UFCCStopRulePanel::OnCarClicked(UFCCCarModeButton* Button, EFCCCarMode Mode)
{
	UFCCTimeTableEditSession* Session = UFCCTimeTableEditSession::GetActive();
	if (!Button || !Session || !mStationWidget.IsValid())
	{
		return;
	}
	mRule.SetCarMode(Button->Index, Mode);
	mRule.Trim();
	Button->SetMode(Mode);
	Session->SetRule(mStationWidget.Get(), mRule);
}

void UFCCStopRulePanel::HandleDiscard()
{
	UFCCTimeTableEditSession* Session = UFCCTimeTableEditSession::GetActive();
	if (Session && mStationWidget.IsValid() && !(mRule == mRuleWhenOpened))
	{
		Session->SetRule(mStationWidget.Get(), mRuleWhenOpened);
	}
	mRule = mRuleWhenOpened;
	mStationWidget.Reset();
}

#undef LOCTEXT_NAMESPACE
