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

// Unreal macros that commonly collide with identifiers. Defined here (and force-included into every
// translation unit by build.sh) so the standalone build fails the same way Unreal would.
#define PI (3.1415926535897932f)
#define HALF_PI (1.57079632679f)
#define INV_PI (0.31830988618f)
#define SMALL_NUMBER (1.e-8f)
#define KINDA_SMALL_NUMBER (1.e-4f)
#define BIG_NUMBER (3.4e+38f)
#define DELTA (0.00001f)
#define INDEX_NONE (-1)
#define TEXT(x) L##x
#define check(expr) ((void)(expr))
#define verify(expr) ((void)(expr))
#define ensure(expr) (!!(expr))
