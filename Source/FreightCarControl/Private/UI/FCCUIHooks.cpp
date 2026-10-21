#include "UI/FCCUIHooks.h"
#include "FreightCarControl.h"
#include "UI/FCCStopRulePanel.h"
#include "UI/FCCTimeTableEditSession.h"
#include "UI/FCCWagonRulePanel.h"
#include "UI/FCCWidgetUtils.h"
#include "Engine/Engine.h"
#include "Patching/WidgetBlueprintHookManager.h"

namespace
{
	// Creates and registers one hook adding NewWidgetClass to the panel ParentName of the vanilla widget WidgetPath.
	UWidgetBlueprintHookData* AddHook(UObject* Outer, UWidgetBlueprintHookManager* Manager, const TCHAR* WidgetPath, FName ParentName,
		TSubclassOf<UUserWidget> NewWidgetClass, FName NewWidgetName, UWidgetBlueprintHookSlot* Slot = nullptr)
	{
		UWidgetBlueprintHookData* Hook = NewObject<UWidgetBlueprintHookData>(Outer);
		Hook->WidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(WidgetPath));
		Hook->NewWidgetClass = NewWidgetClass;
		Hook->NewWidgetName = NewWidgetName;
		Hook->ParentWidgetName = ParentName;
		Hook->ParentWidgetType = EWidgetBlueprintHookParentType::Direct;
		Hook->SlotConfiguration = Slot;
		if (!Hook->WidgetClass.LoadSynchronous())
		{
			UE_LOG(LogFreightCarControl, Error, TEXT("UI hook %s: vanilla widget %s not found"), *NewWidgetName.ToString(), WidgetPath);
			return nullptr;
		}
		if (!Hook->ResolveParentWidgetOnArchetype())
		{
			UE_LOG(LogFreightCarControl, Error, TEXT("UI hook %s: panel %s not found in %s"), *NewWidgetName.ToString(), *ParentName.ToString(), WidgetPath);
			return nullptr;
		}
		Manager->RegisterWidgetBlueprintHook(Hook);
		UE_LOG(LogFreightCarControl, Log, TEXT("UI hook %s added to %s in %s"), *NewWidgetName.ToString(), *ParentName.ToString(), WidgetPath);
		return Hook;
	}

	// Returns a canvas slot setup that pins a widget to the bottom of a canvas with side margins, sized to its content.
	UWidgetBlueprintHookSlot_Canvas* MakeBottomCanvasSlot(UObject* Outer, float Margin)
	{
		UWidgetBlueprintHookSlot_Canvas* Slot = NewObject<UWidgetBlueprintHookSlot_Canvas>(Outer);
		Slot->LayoutData.Anchors = FAnchors(0.f, 1.f, 1.f, 1.f);
		Slot->LayoutData.Alignment = FVector2D(0.f, 1.f);
		Slot->LayoutData.Offsets = FMargin(Margin, -Margin, Margin, 0.f);
		Slot->bAutoSize = true;
		Slot->ZOrder = 10;
		return Slot;
	}

	// Returns a canvas slot setup for an invisible zero-size widget.
	UWidgetBlueprintHookSlot_Canvas* MakeHiddenCanvasSlot(UObject* Outer)
	{
		UWidgetBlueprintHookSlot_Canvas* Slot = NewObject<UWidgetBlueprintHookSlot_Canvas>(Outer);
		Slot->LayoutData.Offsets = FMargin(0.f, 0.f, 0.f, 0.f);
		return Slot;
	}
}

void FCCUIHooks::Register(UObject* Outer, TArray<UWidgetBlueprintHookData*>& OutHooks)
{
#if WITH_EDITOR
	UE_LOG(LogFreightCarControl, Log, TEXT("Editor build: UI hooks not registered"));
#else
	if (IsRunningDedicatedServer())
	{
		UE_LOG(LogFreightCarControl, Log, TEXT("Dedicated server: UI hooks not registered"));
		return;
	}
	UWidgetBlueprintHookManager* Manager = GEngine ? GEngine->GetEngineSubsystem<UWidgetBlueprintHookManager>() : nullptr;
	if (!Manager)
	{
		UE_LOG(LogFreightCarControl, Error, TEXT("SML widget hook manager missing; no UI"));
		return;
	}
	auto Keep = [&OutHooks](UWidgetBlueprintHookData* Hook)
	{
		if (Hook)
		{
			OutHooks.Add(Hook);
		}
	};
	Keep(AddHook(Outer, Manager, FCCWidgetUtils::TimeTableSetupClass, TEXT("CanvasPanel_0"),
		UFCCTimeTableEditSession::StaticClass(), TEXT("FCC_EditSession"), MakeHiddenCanvasSlot(Outer)));
	Keep(AddHook(Outer, Manager, FCCWidgetUtils::StationSettingsClass, TEXT("VerticalBox_58"),
		UFCCStopRulePanel::StaticClass(), TEXT("FCC_StopRulePanel")));
	Keep(AddHook(Outer, Manager, FCCWidgetUtils::FreightWagonWidgetClass, TEXT("VerticalBox_0"),
		UFCCWagonRulePanel::StaticClass(), TEXT("FCC_WagonRulePanel")));
	Keep(AddHook(Outer, Manager, FCCWidgetUtils::FreightWagonWidgetClass, TEXT("CanvasPanel_0"),
		UFCCWagonRulePanel::StaticClass(), TEXT("FCC_WagonRulePanelFluid"), MakeBottomCanvasSlot(Outer, 24.f)));
	UE_LOG(LogFreightCarControl, Log, TEXT("%d of 4 UI hooks registered"), OutHooks.Num());
#endif
}
