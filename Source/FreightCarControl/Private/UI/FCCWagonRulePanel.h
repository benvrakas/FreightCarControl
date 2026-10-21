#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FCCWagonRulePanel.generated.h"

class AFGFreightWagon;
class UFCCCarModeButton;
class UTextBlock;
class UVerticalBox;

// "Timetable rules" section added to the vanilla freight car menu (Widget_FreightWagon). Lists the train's stops with
// this car's mode at each; a click cycles the mode and saves it straight away.
UCLASS()
class UFCCWagonRulePanel : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	void BuildTree();

	// Returns the freight car whose menu this is.
	AFGFreightWagon* GetWagon() const;

	// Rebuilds the stop list when the train, its stops or the rules change.
	void Poll();

	void RebuildRows();

	void OnStopClicked(UFCCCarModeButton* Button);

	UFUNCTION()
	void HandleRulesChanged();

	UPROPERTY()
	TObjectPtr<UVerticalBox> mRoot;

	UPROPERTY()
	TObjectPtr<UTextBlock> mHint;

	UPROPERTY()
	TObjectPtr<UVerticalBox> mRows;

	UPROPERTY()
	TArray<TObjectPtr<UFCCCarModeButton>> mButtons;

	// Stations of the train's stops and car index when the rows were built; a change triggers a rebuild.
	TArray<TWeakObjectPtr<UObject>> mShownStations;
	int32 mShownCarIndex = INDEX_NONE;
	bool bDirty = true;
	FTimerHandle mPollTimer;
};
