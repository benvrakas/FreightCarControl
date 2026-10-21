#pragma once

#include "CoreMinimal.h"

class AFGTrainStationIdentifier;
class UButton;
class UTextBlock;
class UUserWidget;
class UWidget;
class UWidgetTree;

// Helpers for building UI from the game's own Blueprint widgets, which C++ can only reach through reflection.
namespace FCCWidgetUtils
{
	// Class paths of the vanilla widgets the mod uses or hooks.
	extern const TCHAR* StandardButtonClass;
	extern const TCHAR* TrainMenuLabelClass;
	extern const TCHAR* StationSettingsClass;
	extern const TCHAR* TimeTableSetupClass;
	extern const TCHAR* SetupStationClassName;
	extern const TCHAR* FreightWagonWidgetClass;

	// Loads a widget class by path; logs and returns nullptr if it's missing.
	UClass* LoadWidgetClass(const TCHAR* Path);

	// Creates a vanilla standard button in Tree, or nullptr if the class is missing.
	UUserWidget* CreateStandardButton(UWidgetTree* Tree, const FText& Text);

	// Creates a vanilla train menu heading in Tree, or nullptr if the class is missing.
	UUserWidget* CreateMenuLabel(UWidgetTree* Tree, const FText& Text);

	// Calls the Blueprint function SetText(Text) on Widget; returns false if it has none.
	bool SetWidgetText(UUserWidget* Widget, const FText& Text);

	// Returns the UButton inside a vanilla standard button (its mButton), or nullptr.
	UButton* GetInnerButton(UUserWidget* StandardButton);

	// Binds Target's UFUNCTION FunctionName to Source's Blueprint event dispatcher DelegateName; returns false if either is missing.
	bool BindEventDispatcher(UObject* Source, FName DelegateName, UObject* Target, FName FunctionName);

	// Removes Target's FunctionName from Source's dispatcher DelegateName.
	void UnbindEventDispatcher(UObject* Source, FName DelegateName, UObject* Target, FName FunctionName);

	// Returns the object stored in Owner's object property PropertyName, or nullptr.
	UObject* GetObjectProperty(const UObject* Owner, FName PropertyName);

	// Returns the station a timetable setup station widget stands for (its mStation), or nullptr.
	AFGTrainStationIdentifier* GetSetupStationStation(const UObject* SetupStationWidget);

	// Returns all widgets of Root's tree and nested user widget trees whose class name is ClassName, in panel order.
	TArray<UUserWidget*> FindNestedWidgetsByClassName(UUserWidget* Root, const TCHAR* ClassName);

	// Copies font and colour from the TextBlock named SourceName in Root's tree (or the first TextBlock found) to Target,
	// limiting the font size to MaxSize when it is above 0.
	void CopyTextStyle(UTextBlock* Target, UUserWidget* Root, FName SourceName = NAME_None, int32 MaxSize = 0);

	// Returns the nearest user widget that owns Widget, skipping Widget itself.
	UUserWidget* GetOwningUserWidget(const UWidget* Widget);
}
