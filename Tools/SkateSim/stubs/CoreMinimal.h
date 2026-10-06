// Minimal stand-in for Unreal's CoreMinimal.h so the engine-independent skating core
// (Source/IceFootball/Skate/Core) can be compiled and tested without Unreal.
#pragma once
#include <cstdint>

typedef int32_t int32;
typedef uint8_t uint8;

#define USTRUCT(...)
#define UENUM(...)
#define UPROPERTY(...)
#define UMETA(...)
#define GENERATED_BODY()
