#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FCCTypes.h"
#include "FCCStopRulePanel.generated.h"

class AFGTrain;
class UFCCCarModeButton;
class UTextBlock;
class UVerticalBox;
class UWrapBox;

// "Freight cars" section added to the vanilla timetable stop settings (BPW_Train_TimeTable_StationSettings).
// Shows one fixed-width button per freight car of the train in a scrolling grid; each click cycles that car's mode at
// this stop in the editor's draft.
UCLASS()
class UFCCStopRulePanel : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	void BuildTree();

	// Follows the stop being edited and the train's cars, rebuilding the buttons when either changes.
	void Poll();

	// Recreates one button per freight car from mRule.
	void RebuildButtons(const AFGTrain* Train);

	void OnCarClicked(UFCCCarModeButton* Button);

	UFUNCTION()
	void HandleDiscard();

	UPROPERTY()
	TObjectPtr<UTextBlock> mHint;

	UPROPERTY()
	TObjectPtr<UWrapBox> mCars;

	// State the buttons were built from; any change triggers a rebuild.
	TWeakObjectPtr<UUserWidget> mStationWidget;
	TWeakObjectPtr<AFGTrain> mTrain;
	int32 mCarCount = INDEX_NONE;

	FFCCStopRule mRule;
	FFCCStopRule mRuleWhenOpened;
	FTimerHandle mPollTimer;
};
