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
	// Live sessions in construction order. The editor builds more than one, so drafts are shared per train.
	TArray<TWeakObjectPtr<UFCCTimeTableEditSession>> GSessions;

	// Draft rules per train being edited, until the timetable is saved or discarded or its last editor closes.
	TMap<TWeakObjectPtr<AFGTrain>, TMap<TPair<const AFGTrainStationIdentifier*, int32>, FFCCStopRule>> GDrafts;

	FString DescribeStation(const AFGTrainStationIdentifier* Station)
	{
		return Station ? Station->GetStationName().ToString() : FString(TEXT("(none)"));
	}
}

UFCCTimeTableEditSession* UFCCTimeTableEditSession::GetActive()
{
	for (int32 Index = GSessions.Num() - 1; Index >= 0; --Index)
	{
		UFCCTimeTableEditSession* Session = GSessions[Index].Get();
		if (Session && Session->GetTrain())
		{
			return Session;
		}
	}
	return nullptr;
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
	GSessions.RemoveAll([](const TWeakObjectPtr<UFCCTimeTableEditSession>& Session) { return !Session.IsValid(); });
	GSessions.AddUnique(this);
	UUserWidget* Setup = GetSetup();
	bool bBoundSave = FCCWidgetUtils::BindEventDispatcher(Setup, TEXT("OnSave"), this, GET_FUNCTION_NAME_CHECKED(UFCCTimeTableEditSession, HandleSave));
	if (!bBoundSave)
	{
		if (UButton* SaveButton = FCCWidgetUtils::GetInnerButton(Cast<UUserWidget>(FCCWidgetUtils::GetObjectProperty(Setup, TEXT("mSaveButton")))))
		{
			SaveButton->OnClicked.AddUniqueDynamic(this, &UFCCTimeTableEditSession::HandleSave);
			bBoundSave = true;
		}
	}
	const bool bBoundDiscard = FCCWidgetUtils::BindEventDispatcher(Setup, TEXT("OnDiscard"), this, GET_FUNCTION_NAME_CHECKED(UFCCTimeTableEditSession, HandleDiscard));
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
	GSessions.Remove(this);
	const AFGTrain* Train = GetTrain();
	const bool bTrainStillEdited = GSessions.ContainsByPredicate([Train](const TWeakObjectPtr<UFCCTimeTableEditSession>& Session)
	{
		return Session.IsValid() && Session->GetTrain() == Train;
	});
	if (!bTrainStillEdited && GDrafts.Remove(Train) > 0)
	{
		UE_LOG(LogFreightCarControl, Log, TEXT("Timetable editor closed without saving; car rule draft for %s dropped"), *GetNameSafe(Train));
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
	for (UUserWidget* Menu = FCCWidgetUtils::GetOwningUserWidget(Setup); Menu; Menu = FCCWidgetUtils::GetOwningUserWidget(Menu))
	{
		if (AFGTrain* Train = Cast<AFGTrain>(FCCWidgetUtils::GetObjectProperty(Menu, TEXT("mTrain"))))
		{
			return Train;
		}
		if (const AFGRailroadVehicle* Vehicle = Cast<AFGRailroadVehicle>(FCCWidgetUtils::GetObjectProperty(Menu, TEXT("mInteractObject"))))
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

UFCCTimeTableEditSession::FStopKey UFCCTimeTableEditSession::GetKey(UUserWidget* StationWidget, int32* OutListIndex) const
{
	const AFGTrainStationIdentifier* Station = FCCWidgetUtils::GetSetupStationStation(StationWidget);
	const TArray<UUserWidget*> Widgets = GetStationWidgets();
	int32 Occurrence = 0;
	for (int32 Index = 0; Index < Widgets.Num(); ++Index)
	{
		if (Widgets[Index] == StationWidget)
		{
			if (OutListIndex)
			{
				*OutListIndex = Index;
			}
			return FStopKey(Station, Occurrence);
		}
		Occurrence += FCCWidgetUtils::GetSetupStationStation(Widgets[Index]) == Station ? 1 : 0;
	}
	if (OutListIndex)
	{
		*OutListIndex = INDEX_NONE;
	}
	UE_LOG(LogFreightCarControl, Verbose, TEXT("Timetable editor: %s isn't in the timetable list; using the first stop at %s"),
		*GetNameSafe(StationWidget), *DescribeStation(Station));
	return FStopKey(Station, 0);
}

FFCCStopRule UFCCTimeTableEditSession::GetServerRule(const FStopKey& Key, int32 ListIndex) const
{
	const AFGTrain* Train = GetTrain();
	const AFGRailroadTimeTable* TimeTable = Train ? Train->GetTimeTable() : nullptr;
	const AFCCRuleSubsystem* Subsystem = AFCCRuleSubsystem::Get(this);
	if (!TimeTable || !Subsystem)
	{
		return FFCCStopRule();
	}
	int32 Seen = 0;
	for (int32 StopIndex = 0; StopIndex < TimeTable->GetNumStops(); ++StopIndex)
	{
		if (TimeTable->GetStop(StopIndex).Station == Key.Key && Seen++ == Key.Value)
		{
			return Subsystem->GetStopRule(TimeTable, StopIndex);
		}
	}
	if (ListIndex != INDEX_NONE && GetStationWidgets().Num() == TimeTable->GetNumStops())
	{
		return Subsystem->GetStopRule(TimeTable, ListIndex);
	}
	return FFCCStopRule();
}

const UFCCTimeTableEditSession::FDraft* UFCCTimeTableEditSession::FindDraft() const
{
	return GDrafts.Find(GetTrain());
}

FFCCStopRule UFCCTimeTableEditSession::GetRule(UUserWidget* StationWidget) const
{
	int32 ListIndex = INDEX_NONE;
	const FStopKey Key = GetKey(StationWidget, &ListIndex);
	const FDraft* Draft = FindDraft();
	const FFCCStopRule* Edited = Draft ? Draft->Find(Key) : nullptr;
	return Edited ? *Edited : GetServerRule(Key, ListIndex);
}

void UFCCTimeTableEditSession::SetRule(UUserWidget* StationWidget, const FFCCStopRule& Rule)
{
	AFGTrain* Train = GetTrain();
	if (!Train)
	{
		return;
	}
	const FStopKey Key = GetKey(StationWidget);
	FDraft& Draft = GDrafts.FindOrAdd(Train);
	Draft.Add(Key, Rule);
	UE_LOG(LogFreightCarControl, Log, TEXT("Timetable editor: %s stop at %s #%d set to [%s] (%d stops in draft)"),
		*Train->GetTrainName().ToString(), *DescribeStation(Key.Key), Key.Value + 1, *Rule.ToCodes(), Draft.Num());
}

void UFCCTimeTableEditSession::HandleSave()
{
	AFGTrain* Train = GetTrain();
	FDraft Draft;
	if (!Train || !GDrafts.RemoveAndCopyValue(Train, Draft))
	{
		UE_LOG(LogFreightCarControl, Verbose, TEXT("Timetable saved for %s with no car rule edits"), *GetNameSafe(Train));
		return;
	}
	const TArray<UUserWidget*> Widgets = GetStationWidgets();
	TArray<int32> StopIndices;
	TArray<FFCCStopRule> Rules;
	TMap<const AFGTrainStationIdentifier*, int32> Occurrences;
	FString Summary;
	for (int32 Index = 0; Index < Widgets.Num(); ++Index)
	{
		const AFGTrainStationIdentifier* Station = FCCWidgetUtils::GetSetupStationStation(Widgets[Index]);
		const FStopKey Key(Station, Occurrences.FindOrAdd(Station)++);
		const FFCCStopRule* Edited = Draft.Find(Key);
		Summary += FString::Printf(TEXT(" [%d. %s: %s]"), Index + 1, *DescribeStation(Station), Edited ? *Edited->ToCodes() : TEXT("unchanged"));
		if (Edited)
		{
			StopIndices.Add(Index);
			Rules.Add(*Edited);
		}
	}
	UE_LOG(LogFreightCarControl, Log, TEXT("Timetable saved for %s: sending %d edited stop rules of %d stops:%s"),
		*Train->GetTrainName().ToString(), Rules.Num(), Widgets.Num(), *Summary);
	if (Rules.Num() < Draft.Num())
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("Timetable editor: %d edited stops are no longer in %s's list and were dropped"),
			Draft.Num() - Rules.Num(), *Train->GetTrainName().ToString());
	}

	// Sent a tick later so it reaches the server after the editor's own save on the same connection.
	TWeakObjectPtr<AFGTrain> WeakTrain = Train;
	TWeakObjectPtr<UWorld> WeakWorld = GetWorld();
	const int32 StopCount = Widgets.Num();
	auto Send = [WeakTrain, WeakWorld, StopCount, StopIndices, Rules]()
	{
		UFCCRemoteCallObject* RCO = UFCCRemoteCallObject::GetLocal(WeakWorld.Get());
		if (RCO && WeakTrain.IsValid())
		{
			RCO->Server_SetEditorRules(WeakTrain.Get(), StopCount, StopIndices, Rules);
		}
	};
	if (UWorld* World = GetWorld())
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
	const AFGTrain* Train = GetTrain();
	if (GDrafts.Remove(Train) > 0)
	{
		UE_LOG(LogFreightCarControl, Log, TEXT("Timetable editor discarded; car rule draft for %s dropped"), *GetNameSafe(Train));
	}
}
