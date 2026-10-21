#pragma once

#include "CoreMinimal.h"

// Keeps stored stop rules aligned with a time table when its stops are added, removed or replaced.
class FFCCTimeTableHooks
{
public:
	static void InstallHooks();
};
