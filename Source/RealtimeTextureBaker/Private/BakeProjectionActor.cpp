#include "BakeProjectionActor.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/World.h"
#include "TextureBakeSubsystem.h"

ABakeProjectionActor::ABakeProjectionActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickInterval = 0.0f;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	DepthCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("DepthCapture"));
	DepthCapture->SetupAttachment(RootComponent);
	DepthCapture->bCaptureEveryFrame = false;
	DepthCapture->bCaptureOnMovement = false;
	DepthCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
	DepthCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
}

void ABakeProjectionActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!DepthCapture)
	{
		DepthCapture = NewObject<USceneCaptureComponent2D>(this, TEXT("DepthCapture"));
		DepthCapture->SetupAttachment(RootComponent);
		DepthCapture->RegisterComponent();
		DepthCapture->bCaptureEveryFrame = false;
		DepthCapture->bCaptureOnMovement = false;
		DepthCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
		DepthCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	}
}

void ABakeProjectionActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bAutoBake)
	{
		BakeCurrentMode();
	}
}

#if WITH_EDITOR
bool ABakeProjectionActor::ShouldTickIfViewportsOnly() const
{
	return bAutoBake && BakeMode != ERealtimeTextureBakeMode::None;
}
#endif

UTextureRenderTarget2D* ABakeProjectionActor::AllocateRenderTarget()
{
	UWorld* World = GetWorld();
	UTextureBakeSubsystem* BakeSubsystem = World ? World->GetSubsystem<UTextureBakeSubsystem>() : nullptr;
	if (!BakeSubsystem)
	{
		return nullptr;
	}

	OutputRenderTarget = BakeSubsystem->CreateBakeRenderTarget(Settings);
	return OutputRenderTarget;
}

UTextureRenderTarget2D* ABakeProjectionActor::AllocateDepthRenderTarget(float AspectRatio)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const int32 Resolution = Settings.GetClampedResolution();
	const float ClampedAspectRatio = FMath::Max(AspectRatio, 0.0001f);
	int32 Width = Resolution;
	int32 Height = Resolution;
	if (ClampedAspectRatio >= 1.0f)
	{
		Height = FMath::Max(1, FMath::RoundToInt(Resolution / ClampedAspectRatio));
	}
	else
	{
		Width = FMath::Max(1, FMath::RoundToInt(Resolution * ClampedAspectRatio));
	}

	if (DepthRenderTarget)
	{
		if (DepthRenderTarget->SizeX != Width || DepthRenderTarget->SizeY != Height || DepthRenderTarget->RenderTargetFormat != Settings.DepthRenderTargetFormat)
		{
			DepthRenderTarget = nullptr;
		}
	}

	if (!DepthRenderTarget)
	{
		DepthRenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
			World,
			Width,
			Height,
			Settings.DepthRenderTargetFormat,
			FLinearColor::Black,
			false
		);
	}

	if (DepthCapture)
	{
		DepthCapture->TextureTarget = DepthRenderTarget;
	}

	return DepthRenderTarget;
}

void ABakeProjectionActor::AllocateDepthRenderTargetEditor()
{
	AllocateDepthRenderTarget();
}

void ABakeProjectionActor::UpdateDepthCaptureFromProjectionCamera()
{
	if (!DepthCapture || !ProjectionCamera)
	{
		return;
	}

	UCameraComponent* CameraComponent = ProjectionCamera->GetCameraComponent();
	if (!CameraComponent)
	{
		return;
	}

	const FTransform CameraTransform = CameraComponent->GetComponentTransform();
	DepthCapture->SetWorldTransform(CameraTransform);
	DepthCapture->ProjectionType = ECameraProjectionMode::Perspective;
	DepthCapture->FOVAngle = CameraComponent->FieldOfView;
	DepthCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
	DepthCapture->TextureTarget = DepthRenderTarget;

	DepthCapture->ShowOnlyActors.Empty();
	if (TargetMesh)
	{
		DepthCapture->ShowOnlyActors.Add(TargetMesh);
	}
}

bool ABakeProjectionActor::BakeUVTexture()
{
	if (!OutputRenderTarget)
	{
		AllocateRenderTarget();
	}

	UWorld* World = GetWorld();
	UTextureBakeSubsystem* BakeSubsystem = World ? World->GetSubsystem<UTextureBakeSubsystem>() : nullptr;
	UStaticMeshComponent* TargetMeshComponent = TargetMesh ? TargetMesh->GetStaticMeshComponent() : nullptr;
	return BakeSubsystem && BakeSubsystem->BakeUVTextureToUV(TargetMeshComponent, SourceTexture, OutputRenderTarget, Settings);
}

bool ABakeProjectionActor::BakeCameraProjection()
{
	if (!OutputRenderTarget)
	{
		AllocateRenderTarget();
	}

	float DepthAspectRatio = 1.0f;
	if (ProjectionCamera)
	{
		if (UCameraComponent* CameraComponent = ProjectionCamera->GetCameraComponent())
		{
			FMinimalViewInfo ViewInfo;
			CameraComponent->GetCameraView(0.0f, ViewInfo);
			DepthAspectRatio = FMath::Max(ViewInfo.AspectRatio, 0.0001f);
		}
	}

	AllocateDepthRenderTarget(DepthAspectRatio);
	UpdateDepthCaptureFromProjectionCamera();
	if (!DepthCapture)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: DepthCapture component is missing on BakeProjectionActor '%s'."), *GetName());
	}
	if (DepthCapture && DepthRenderTarget)
	{
		DepthCapture->CaptureScene();
	}

	UWorld* World = GetWorld();
	UTextureBakeSubsystem* BakeSubsystem = World ? World->GetSubsystem<UTextureBakeSubsystem>() : nullptr;
	UStaticMeshComponent* TargetMeshComponent = TargetMesh ? TargetMesh->GetStaticMeshComponent() : nullptr;
	return BakeSubsystem && BakeSubsystem->BakeCameraProjectionToUV(TargetMeshComponent, ProjectionCamera, SourceTexture, OutputRenderTarget, DepthRenderTarget, Settings);
}

void ABakeProjectionActor::BakeCameraProjectionEditor()
{
	BakeCameraProjection();
}

bool ABakeProjectionActor::BakeCurrentMode()
{
	switch (BakeMode)
	{
	case ERealtimeTextureBakeMode::BakeUV:
		return BakeUVTexture();
	case ERealtimeTextureBakeMode::BakeCamera:
		return BakeCameraProjection();
	case ERealtimeTextureBakeMode::None:
	default:
		return false;
	}
}

void ABakeProjectionActor::BakeCurrentModeEditor()
{
	BakeCurrentMode();
}

void ABakeProjectionActor::ClearOutput()
{
	UWorld* World = GetWorld();
	UTextureBakeSubsystem* BakeSubsystem = World ? World->GetSubsystem<UTextureBakeSubsystem>() : nullptr;
	if (BakeSubsystem && OutputRenderTarget)
	{
		BakeSubsystem->ClearBakeRenderTarget(OutputRenderTarget, Settings.ClearColor);
	}
}
