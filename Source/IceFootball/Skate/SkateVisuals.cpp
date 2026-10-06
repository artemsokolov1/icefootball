#include "Skate/SkateVisuals.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "IceFootball.h"

namespace SkateVisuals
{
	UStaticMesh* LoadBasicShape(const TCHAR* ShapeName)
	{
		const FString Path = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), ShapeName, ShapeName);
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
		if (!Mesh)
		{
			UE_LOG(LogIceSkate, Error, TEXT("Missing engine mesh %s"), *Path);
		}
		return Mesh;
	}

	UMaterialInterface* BaseMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid())
		{
			Cached = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		}
		return Cached.Get();
	}

	UMaterialInstanceDynamic* MakeColorMaterial(UObject* Outer, const FLinearColor& Color)
	{
		UMaterialInterface* Base = BaseMaterial();
		if (!Base)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Outer);
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		return Mid;
	}

	UStaticMeshComponent* AddPart(AActor* Owner, USceneComponent* Parent, const TCHAR* ShapeName, const FLinearColor& Color, bool bCastShadow)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
		Part->SetStaticMesh(LoadBasicShape(ShapeName));
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(bCastShadow);
		Part->SetMobility(EComponentMobility::Movable);
		if (Parent)
		{
			Part->SetupAttachment(Parent);
		}
		Part->RegisterComponent();
		Part->SetMaterial(0, MakeColorMaterial(Part, Color));
		return Part;
	}

	void SetSegment(UStaticMeshComponent* Part, const FVector& A, const FVector& B, float ThicknessX, float ThicknessY)
	{
		if (!Part)
		{
			return;
		}
		const FVector Delta = B - A;
		const double Length = FMath::Max(Delta.Size(), 0.1);
		const FRotator Rotation = FRotationMatrix::MakeFromZ(Delta / Length).Rotator();
		Part->SetRelativeTransform(FTransform(Rotation, (A + B) * 0.5, FVector(ThicknessX / 100.0, ThicknessY / 100.0, Length / 100.0)));
	}
}
