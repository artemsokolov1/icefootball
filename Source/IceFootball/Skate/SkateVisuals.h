// Ice skating prototype - placeholder visuals built from engine basic shapes.
// No project assets are required: meshes are /Engine/BasicShapes/*, colours come from a
// dynamic instance of /Engine/BasicShapes/BasicShapeMaterial ("Color" parameter).
#pragma once

#include "CoreMinimal.h"

class AActor;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class USceneComponent;

namespace SkateVisuals
{
	/** "Cube", "Sphere", "Cylinder", "Cone", "Plane". All are 100 cm, pivot at centre. */
	UStaticMesh* LoadBasicShape(const TCHAR* ShapeName);

	UMaterialInterface* BaseMaterial();

	UMaterialInstanceDynamic* MakeColorMaterial(UObject* Outer, const FLinearColor& Color);

	/** Creates, attaches and registers a non-colliding placeholder mesh part. */
	UStaticMeshComponent* AddPart(AActor* Owner, USceneComponent* Parent, const TCHAR* ShapeName, const FLinearColor& Color, bool bCastShadow = true);

	/** Places a 100 cm basic-shape cylinder/cube so it spans A->B (relative to its parent) with the given thickness. */
	void SetSegment(UStaticMeshComponent* Part, const FVector& A, const FVector& B, float ThicknessX, float ThicknessY);
}
