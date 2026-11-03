#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FCCMapTrainButton.generated.h"

class AFGTrain;
class UButton;
class UFGActorRepresentation;

// Icon button added to the map tab's list rows (BPW_MapFilterButton). Shown only on train rows; a click closes the map
// and opens that train's locomotive menu, as if the player were driving it.
UCLASS()
class UFCCMapTrainButton : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	// Creates the button and its icon.
	void BuildTree();

	// Shows the button if the row stands for a train; retries on the next ticks while the row has no representation yet.
	void Refresh();

	// Returns the representation of the row this button is in, or nullptr.
	UFGActorRepresentation* GetRepresentation() const;

	// Returns the train the row stands for, or nullptr.
	AFGTrain* FindTrain() const;

	UFUNCTION()
	void HandleClicked();

	UPROPERTY()
	TObjectPtr<UButton> mButton;

	FTimerHandle RefreshTimer;
	int32 RefreshAttempts = 0;
};
