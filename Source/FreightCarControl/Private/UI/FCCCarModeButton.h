#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FCCTypes.h"
#include "FCCCarModeButton.generated.h"

class UButton;
class USizeBox;
class UTextBlock;

// A vanilla standard button that shows a freight car mode and reports clicks.
UCLASS()
class UFCCCarModeButton : public UUserWidget
{
	GENERATED_BODY()

public:
	// Sets the text shown with the mode, e.g. "Car 2".
	void SetLabel(const FText& InLabel);

	// Sets the mode shown.
	void SetMode(EFCCCarMode InMode);

	EFCCCarMode GetMode() const { return Mode; }

	// Makes the button exactly Width wide, so buttons line up in columns; 0 sizes it to its text.
	void SetFixedWidth(float Width);

	// Called with this button when it is clicked.
	TFunction<void(UFCCCarModeButton*)> OnClickedHandler;

	// Free value for the owner, e.g. the car or stop index.
	int32 Index = INDEX_NONE;

	// Shows "Mode | Label" instead of "Label: Mode", so the mode stays visible when a long label is cut off.
	bool bModeFirst = false;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	// Creates the vanilla button, or a plain one if the vanilla class is missing, inside a size box.
	void BuildTree();

	// Updates the button text and width.
	void Refresh();

	UFUNCTION()
	void HandleClicked();

	UPROPERTY()
	TObjectPtr<USizeBox> mFrame;

	UPROPERTY()
	TObjectPtr<UUserWidget> mVanillaButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> mFallbackText;

	FText Label;
	EFCCCarMode Mode = EFCCCarMode::Platform;
	float FixedWidth = 0.f;
};
