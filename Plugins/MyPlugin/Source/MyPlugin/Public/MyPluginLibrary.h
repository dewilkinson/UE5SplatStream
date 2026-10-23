#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MyPluginLibrary.generated.h"

UCLASS()
class MYPLUGIN_API UMyPluginLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Example function callable from C++ and Blueprints. */
	UFUNCTION(BlueprintPure, Category = "MyPlugin")
	static int32 AddNumbers(int32 A, int32 B);
};
