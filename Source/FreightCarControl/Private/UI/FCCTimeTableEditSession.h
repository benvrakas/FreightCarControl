#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FCCTypes.h"
#include "FCCTimeTableEditSession.generated.h"

class AFGRailroadTimeTable;
class AFGTrain;
class AFGTrainStationIdentifier;

// Invisible widget added to the vanilla timetable editor (BPW_Train_TimeTable_Setup). Keeps the car rules the player
// edits, keyed by (station, how many times that station appears earlier in the list), because the editor recreates its
// stop widgets; sends all rules to the server when the timetable is saved.
UCLASS()
class UFCCTimeTableEditSession : public UUserWidget
{
	GENERATED_BODY()

public:
	using FStopKey = TPair<const AFGTrainStationIdentifier*, int32>;

	// Returns the session of the timetable editor currently open, or nullptr.
	static UFCCTimeTableEditSession* GetActive();

	// Returns the train being edited.
	AFGTrain* GetTrain() const;

	// Returns the rule of the stop shown by StationWidget (a BPW_Train_TimeTable_SetupStation).
	FFCCStopRule GetRule(UUserWidget* StationWidget) const;

	// Sets the rule of the stop shown by StationWidget; sent to the server when the timetable is saved.
	void SetRule(UUserWidget* StationWidget, const FFCCStopRule& Rule);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	// Returns the timetable editor this session belongs to.
	UUserWidget* GetSetup() const;

	// Returns the stop widgets of the editor's timetable list, in order.
	TArray<UUserWidget*> GetStationWidgets() const;

	// Returns StationWidget's key in the current list.
	FStopKey GetKey(UUserWidget* StationWidget) const;

	// Returns the server's rule for the stop with Key, or an empty rule.
	FFCCStopRule GetServerRule(const FStopKey& Key) const;

	UFUNCTION()
	void HandleSave();

	UFUNCTION()
	void HandleDiscard();

	TMap<FStopKey, FFCCStopRule> mEdits;
	bool bBoundSave = false;
	bool bBoundDiscard = false;
};
