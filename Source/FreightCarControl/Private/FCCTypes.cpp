#include "FCCTypes.h"

#define LOCTEXT_NAMESPACE "FreightCarControl"

FText FCCCarMode::GetDisplayName(EFCCCarMode Mode)
{
	switch (Mode)
	{
	case EFCCCarMode::Skip:
		return LOCTEXT("ModeSkip", "Skip");
	case EFCCCarMode::Load:
		return LOCTEXT("ModeLoad", "Load");
	case EFCCCarMode::Unload:
		return LOCTEXT("ModeUnload", "Unload");
	default:
		return LOCTEXT("ModePlatform", "Platform");
	}
}

const TCHAR* FCCCarMode::GetCode(EFCCCarMode Mode)
{
	switch (Mode)
	{
	case EFCCCarMode::Skip:
		return TEXT("-");
	case EFCCCarMode::Load:
		return TEXT("L");
	case EFCCCarMode::Unload:
		return TEXT("U");
	default:
		return TEXT("P");
	}
}

TOptional<EFCCCarMode> FCCCarMode::FromName(const FString& Name)
{
	const UEnum* Enum = StaticEnum<EFCCCarMode>();
	for (int32 Index = 0; Index < static_cast<int32>(EFCCCarMode::Count); ++Index)
	{
		if (Name.Equals(Enum->GetNameStringByIndex(Index), ESearchCase::IgnoreCase))
		{
			return static_cast<EFCCCarMode>(Enum->GetValueByIndex(Index));
		}
	}
	return {};
}

#undef LOCTEXT_NAMESPACE
