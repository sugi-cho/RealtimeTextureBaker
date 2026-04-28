#include "BakeProjectionActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "TextureBakeSubsystem.h"

ABakeProjectionActor::ABakeProjectionActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickInterval = 0.0f;
}

void ABakeProjectionActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	BakeCurrentMode();
}

#if WITH_EDITOR
bool ABakeProjectionActor::ShouldTickIfViewportsOnly() const
{
	return BakeMode != ERealtimeTextureBakeMode::None;
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

	UWorld* World = GetWorld();
	UTextureBakeSubsystem* BakeSubsystem = World ? World->GetSubsystem<UTextureBakeSubsystem>() : nullptr;
	UStaticMeshComponent* TargetMeshComponent = TargetMesh ? TargetMesh->GetStaticMeshComponent() : nullptr;
	return BakeSubsystem && BakeSubsystem->BakeCameraProjectionToUV(TargetMeshComponent, ProjectionCamera, SourceTexture, OutputRenderTarget, Settings);
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

void ABakeProjectionActor::ClearOutput()
{
	UWorld* World = GetWorld();
	UTextureBakeSubsystem* BakeSubsystem = World ? World->GetSubsystem<UTextureBakeSubsystem>() : nullptr;
	if (BakeSubsystem && OutputRenderTarget)
	{
		BakeSubsystem->ClearBakeRenderTarget(OutputRenderTarget, Settings.ClearColor);
	}
}
