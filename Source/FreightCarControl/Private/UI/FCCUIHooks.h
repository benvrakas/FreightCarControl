#pragma once

#include "CoreMinimal.h"

class UObject;
class UWidgetBlueprintHookData;

// Adds the mod's widgets to the vanilla timetable editor, stop settings and freight car menu.
namespace FCCUIHooks
{
	// Registers the widget blueprint hooks; Outer owns the hook data, which is also added to OutHooks to keep it alive.
	void Register(UObject* Outer, TArray<UWidgetBlueprintHookData*>& OutHooks);
}
