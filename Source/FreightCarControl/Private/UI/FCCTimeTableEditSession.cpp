#include "UI/FCCTimeTableEditSession.h"
#include "FCCRemoteCallObject.h"
#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "UI/FCCWidgetUtils.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "FGRailroadTimeTable.h"
#include "FGTrain.h"
#include "FGTrainStationIdentifier.h"
#include "TimerManager.h"

namespace
{
	TWeakObjectPtr<UFCCTimeTableEditSession> GActiveSession;

	// Seconds between checks for newly listed stops.
	constexpr float SessionPollInterval = 0.25f;
}

UFCCTimeTableEditSession* UFCCTimeTableEditSession::GetActive()
{
	return GActiveSession.Get();
}

TSharedRef<SWidget> UFCCTimeTableEditSession::RebuildWidget()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (!WidgetTree->RootWidget)
	{
		USizeBox* Empty = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Empty->SetWidthOverride(0.f);
		Empty->SetHeightOverride(0.f);
		WidgetTree->RootWidget = Empty;
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
	return Super::RebuildWidget();
}

void UFCCTimeTableEditSession::NativeConstruct()
{
	Super::NativeConstruct();
	GActiveSession = this;
	UUserWidget* Setup = GetSetup();
	bBoundSave = FCCWidgetUtils::BindEventDispatcher(Setup, TEXT("OnSave"), this, GET_FUNCTION_NAME_CHECKED(UFCCTimeTableEditSession, HandleSave));
	if (!bBoundSave)
	{
		if (UButton* SaveButton = FCCWidgetUtils::GetInnerButton(Cast<UUserWidget>(FCCWidgetUtils::GetObjectProperty(Setup, TEXT("mSaveButton")))))
		{
			SaveButton->OnClicked.AddUniqueDynamic(this, &UFCCTimeTableEditSession::HandleSave);
			bBoundSave = true;
		}
	}
	bBoundDiscard = FCCWidgetUtils::BindEventDispatcher(Setup, TEXT("OnDiscard"), this, GET_FUNCTION_NAME_CHECKED(UFCCTimeTableEditSession, HandleDiscard));
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(mPollTimer, this, &UFCCTimeTableEditSession::Poll, SessionPollInterval, true);
	}
	UE_LOG(LogFreightCarControl, Log, TEXT("Timetable editor session started on %s (save hook: %s, discard hook: %s, train: %s)"),
		*GetNameSafe(Setup), bBoundSave ? TEXT("yes") : TEXT("NO"), bBoundDiscard ? TEXT("yes") : TEXT("no"), *GetNameSafe(GetTrain()));
	if (!bBoundSave)
	{
		UE_LOG(LogFreightCarControl, Error, TEXT("Timetable editor: couldn't find its save event; car rules set in the editor won't be saved"));
	}
}

void UFCCTimeTableEditSession::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(mPollTimer);
	}
	UUserWidget* Setup = GetSetup();
	FCCWidgetUtils::UnbindEventDispatcher(Setup, TEXT("OnSave"), this, GET_FUNCTION_NAME_CHECKED(UFCCTimeTableEditSession, HandleSave));
	FCCWidgetUtils::UnbindEventDispatcher(Setup, TEXT("OnDiscard"), this, GET_FUNCTION_NAME_CHECKED(UFCCTimeTableEditSession, HandleDiscard));
	if (GActiveSession == this)
	{
		GActiveSession.Reset();
	}
	mRules.Reset();
	Super::NativeDestruct();
}

UUserWidget* UFCCTimeTableEditSession::GetSetup() const
{
	return FCCWidgetUtils::GetOwningUserWidget(this);
}

AFGTrain* UFCCTimeTableEditSession::GetTrain() const
{
	UUserWidget* Setup = GetSetup();
	if (AFGTrain* Train = Cast<AFGTrain>(FCCWidgetUtils::GetObjectProperty(Setup, TEXT("mTrain"))))
	{
		return Train;
	}
	AFGRailroadTimeTable* TimeTable = Cast<AFGRailroadTimeTable>(FCCWidgetUtils::GetObjectProperty(Setup, TEXT("mTimeTable")));
	return TimeTable ? Cast<AFGTrain>(TimeTable->GetOwner()) : nullptr;
}

TArray<UUserWidget*> UFCCTimeTableEditSession::GetStationWidgets() const
{
	UUserWidget* Setup = GetSetup();
	UUserWidget* List = Cast<UUserWidget>(FCCWidgetUtils::GetObjectProperty(Setup, TEXT("mCurrentTimeTable")));
	return FCCWidgetUtils::FindNestedWidgetsByClassName(List ? List : Setup, FCCWidgetUtils::SetupStationClassName);
}

void UFCCTimeTableEditSession::Poll()
{
	AFGTrain* Train = GetTrain();
	AFGRailroadTimeTable* TimeTable = Train ? Train->GetTimeTable() : nullptr;
	const AFCCRuleSubsystem* Subsystem = AFCCRuleSubsystem::Get(this);
	TArray<FTimeTableStop> ServerStops;
	if (TimeTable)
	{
		TimeTable->GetStops(ServerStops);
	}
	const TArray<UUserWidget*> Widgets = GetStationWidgets();
	TMap<const AFGTrainStationIdentifier*, int32> Occurrence;
	int32 Added = 0;
	for (UUserWidget* Widget : Widgets)
	{
		const AFGTrainStationIdentifier* Station = FCCWidgetUtils::GetSetupStationStation(Widget);
		const int32 Nth = Occurrence.FindOrAdd(Station)++;
		if (mRules.Contains(Widget))
		{
			continue;
		}
		FFCCStopRule Rule;
		int32 Seen = 0;
		for (int32 StopIndex = 0; StopIndex < ServerStops.Num(); ++StopIndex)
		{
			if (ServerStops[StopIndex].Station == Station && Seen++ == Nth)
			{
				Rule = Subsystem ? Subsystem->GetStopRule(TimeTable, StopIndex) : FFCCStopRule();
				break;
			}
		}
		mRules.Add(Widget, Rule);
		++Added;
	}
	if (Added > 0)
	{
		UE_LOG(LogFreightCarControl, Verbose, TEXT("Timetable editor: %d stop widgets listed, %d new"), Widgets.Num(), Added);
	}
	for (auto It = mRules.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

FFCCStopRule UFCCTimeTableEditSession::GetRule(UUserWidget* StationWidget)
{
	if (!mRules.Contains(StationWidget))
	{
		Poll();
	}
	const FFCCStopRule* Rule = mRules.Find(StationWidget);
	return Rule ? *Rule : FFCCStopRule();
}

void UFCCTimeTableEditSession::SetRule(UUserWidget* StationWidget, const FFCCStopRule& Rule)
{
	if (StationWidget)
	{
		mRules.Add(StationWidget, Rule);
	}
}

void UFCCTimeTableEditSession::HandleSave()
{
	Poll();
	AFGTrain* Train = GetTrain();
	TArray<AFGTrainStationIdentifier*> Stations;
	TArray<FFCCStopRule> Rules;
	bool bAnyRule = false;
	for (UUserWidget* Widget : GetStationWidgets())
	{
		Stations.Add(FCCWidgetUtils::GetSetupStationStation(Widget));
		const FFCCStopRule* Rule = mRules.Find(Widget);
		Rules.Add(Rule ? *Rule : FFCCStopRule());
		bAnyRule |= Rule && !Rule->IsDefault();
	}
	UE_LOG(LogFreightCarControl, Log, TEXT("Timetable saved for %s: sending car rules for %d stops (any set: %s)"),
		*GetNameSafe(Train), Rules.Num(), bAnyRule ? TEXT("yes") : TEXT("no"));
	if (!Train)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("Timetable editor has no train; car rules not sent"));
		return;
	}
	UWorld* World = GetWorld();
	TWeakObjectPtr<AFGTrain> WeakTrain = Train;
	TWeakObjectPtr<UWorld> WeakWorld = World;
	auto Send = [WeakTrain, WeakWorld, Stations, Rules]()
	{
		UFCCRemoteCallObject* RCO = UFCCRemoteCallObject::GetLocal(WeakWorld.Get());
		if (RCO && WeakTrain.IsValid())
		{
			RCO->Server_SetAllStopRules(WeakTrain.Get(), Stations, Rules);
		}
	};
	if (World)
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(Send));
	}
	else
	{
		Send();
	}
}

void UFCCTimeTableEditSession::HandleDiscard()
{
	UE_LOG(LogFreightCarControl, Verbose, TEXT("Timetable editor discarded; car rule edits dropped"));
	mRules.Reset();
}
