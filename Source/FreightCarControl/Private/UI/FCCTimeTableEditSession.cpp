#include "UI/FCCTimeTableEditSession.h"
#include "FCCRemoteCallObject.h"
#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "UI/FCCWidgetUtils.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "FGRailroadTimeTable.h"
#include "FGRailroadVehicle.h"
#include "FGTrain.h"
#include "FGTrainStationIdentifier.h"
#include "TimerManager.h"

namespace
{
	TWeakObjectPtr<UFCCTimeTableEditSession> GActiveSession;

	FString DescribeStation(const AFGTrainStationIdentifier* Station)
	{
		return Station ? Station->GetStationName().ToString() : FString(TEXT("(none)"));
	}
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
	mEdits.Reset();
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
	UE_LOG(LogFreightCarControl, Log, TEXT("Timetable editor session started on %s (save hook: %s, discard hook: %s, train: %s)"),
		*GetNameSafe(Setup), bBoundSave ? TEXT("yes") : TEXT("NO"), bBoundDiscard ? TEXT("yes") : TEXT("no"), *GetNameSafe(GetTrain()));
	if (!bBoundSave)
	{
		UE_LOG(LogFreightCarControl, Error, TEXT("Timetable editor: couldn't find its save event; car rules set in the editor won't be saved"));
	}
}

void UFCCTimeTableEditSession::NativeDestruct()
{
	UUserWidget* Setup = GetSetup();
	FCCWidgetUtils::UnbindEventDispatcher(Setup, TEXT("OnSave"), this, GET_FUNCTION_NAME_CHECKED(UFCCTimeTableEditSession, HandleSave));
	FCCWidgetUtils::UnbindEventDispatcher(Setup, TEXT("OnDiscard"), this, GET_FUNCTION_NAME_CHECKED(UFCCTimeTableEditSession, HandleDiscard));
	if (GActiveSession == this)
	{
		GActiveSession.Reset();
	}
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
	if (AFGRailroadTimeTable* TimeTable = Cast<AFGRailroadTimeTable>(FCCWidgetUtils::GetObjectProperty(Setup, TEXT("mTimeTable"))))
	{
		if (AFGTrain* Train = Cast<AFGTrain>(TimeTable->GetOwner()))
		{
			return Train;
		}
	}
	for (UUserWidget* Menu = FCCWidgetUtils::GetOwningUserWidget(Setup); Menu; Menu = FCCWidgetUtils::GetOwningUserWidget(Menu))
	{
		if (AFGTrain* Train = Cast<AFGTrain>(FCCWidgetUtils::GetObjectProperty(Menu, TEXT("mTrain"))))
		{
			return Train;
		}
		if (AFGRailroadVehicle* Vehicle = Cast<AFGRailroadVehicle>(FCCWidgetUtils::GetObjectProperty(Menu, TEXT("mInteractObject"))))
		{
			return Vehicle->GetTrain();
		}
	}
	return nullptr;
}

TArray<UUserWidget*> UFCCTimeTableEditSession::GetStationWidgets() const
{
	UUserWidget* Setup = GetSetup();
	UUserWidget* List = Cast<UUserWidget>(FCCWidgetUtils::GetObjectProperty(Setup, TEXT("mCurrentTimeTable")));
	if (!List)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("Timetable editor: mCurrentTimeTable not found, searching the whole editor"));
	}
	return FCCWidgetUtils::FindNestedWidgetsByClassName(List ? List : Setup, FCCWidgetUtils::SetupStationClassName);
}

UFCCTimeTableEditSession::FStopKey UFCCTimeTableEditSession::GetKey(UUserWidget* StationWidget) const
{
	const AFGTrainStationIdentifier* Station = FCCWidgetUtils::GetSetupStationStation(StationWidget);
	int32 Occurrence = 0;
	bool bFound = false;
	for (UUserWidget* Widget : GetStationWidgets())
	{
		if (Widget == StationWidget)
		{
			bFound = true;
			break;
		}
		Occurrence += FCCWidgetUtils::GetSetupStationStation(Widget) == Station ? 1 : 0;
	}
	if (!bFound)
	{
		UE_LOG(LogFreightCarControl, Verbose, TEXT("Timetable editor: %s isn't in the timetable list; using the first stop at %s"),
			*GetNameSafe(StationWidget), *DescribeStation(Station));
		Occurrence = 0;
	}
	return FStopKey(Station, Occurrence);
}

FFCCStopRule UFCCTimeTableEditSession::GetServerRule(const FStopKey& Key) const
{
	AFGTrain* Train = GetTrain();
	AFGRailroadTimeTable* TimeTable = Train ? Train->GetTimeTable() : nullptr;
	const AFCCRuleSubsystem* Subsystem = AFCCRuleSubsystem::Get(this);
	if (!TimeTable || !Subsystem)
	{
		return FFCCStopRule();
	}
	TArray<FTimeTableStop> Stops;
	TimeTable->GetStops(Stops);
	int32 Seen = 0;
	for (int32 StopIndex = 0; StopIndex < Stops.Num(); ++StopIndex)
	{
		if (Stops[StopIndex].Station == Key.Key && Seen++ == Key.Value)
		{
			return Subsystem->GetStopRule(TimeTable, StopIndex);
		}
	}
	return FFCCStopRule();
}

FFCCStopRule UFCCTimeTableEditSession::GetRule(UUserWidget* StationWidget) const
{
	const FStopKey Key = GetKey(StationWidget);
	if (const FFCCStopRule* Edited = mEdits.Find(Key))
	{
		return *Edited;
	}
	return GetServerRule(Key);
}

void UFCCTimeTableEditSession::SetRule(UUserWidget* StationWidget, const FFCCStopRule& Rule)
{
	const FStopKey Key = GetKey(StationWidget);
	mEdits.Add(Key, Rule);
	UE_LOG(LogFreightCarControl, Log, TEXT("Timetable editor: stop %s #%d edited (%d edited stops)"), *DescribeStation(Key.Key), Key.Value + 1, mEdits.Num());
}

void UFCCTimeTableEditSession::HandleSave()
{
	AFGTrain* Train = GetTrain();
	TArray<AFGTrainStationIdentifier*> Stations;
	TArray<FFCCStopRule> Rules;
	TMap<const AFGTrainStationIdentifier*, int32> Occurrences;
	FString Summary;
	for (UUserWidget* Widget : GetStationWidgets())
	{
		AFGTrainStationIdentifier* Station = FCCWidgetUtils::GetSetupStationStation(Widget);
		const FStopKey Key(Station, Occurrences.FindOrAdd(Station)++);
		const FFCCStopRule* Edited = mEdits.Find(Key);
		const FFCCStopRule Rule = Edited ? *Edited : GetServerRule(Key);
		Stations.Add(Station);
		Rules.Add(Rule);
		FString Codes;
		for (const EFCCCarMode Mode : Rule.Cars)
		{
			Codes += FCCCarMode::GetCode(Mode);
		}
		Summary += FString::Printf(TEXT(" [%s: %s]"), *DescribeStation(Station), *Codes);
	}
	UE_LOG(LogFreightCarControl, Log, TEXT("Timetable saved for %s: sending car rules for %d stops (%d edited):%s"),
		*GetNameSafe(Train), Rules.Num(), mEdits.Num(), *Summary);
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
	mEdits.Reset();
}

void UFCCTimeTableEditSession::HandleDiscard()
{
	UE_LOG(LogFreightCarControl, Log, TEXT("Timetable editor discarded; %d car rule edits dropped"), mEdits.Num());
	mEdits.Reset();
}
