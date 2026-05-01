#include "BakeProjectionActor.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "RenderingThread.h"
#include "TextureBakeSubsystem.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "StaticMeshAttributes.h"
#include "UObject/SavePackage.h"
#endif

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

#if WITH_EDITOR
void ABakeProjectionActor::SaveOutputRenderTargetAsPNG()
{
	if (!OutputRenderTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: OutputRenderTarget is null on BakeProjectionActor '%s'."), *GetName());
		return;
	}

	const FString OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RealtimeTextureBaker"));
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);

	const FString SafeActorName = GetName().Replace(TEXT(" "), TEXT("_"));
	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString OutputFilePath = FPaths::Combine(OutputDirectory, FString::Printf(TEXT("%s_%s.png"), *SafeActorName, *Timestamp));

	TUniquePtr<FArchive> FileWriter(IFileManager::Get().CreateFileWriter(*OutputFilePath));
	if (!FileWriter)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: Failed to create file writer for '%s'."), *OutputFilePath);
		return;
	}

	const bool bSaved = FImageUtils::ExportRenderTarget2DAsPNG(OutputRenderTarget, *FileWriter);
	FileWriter->Close();
	if (!bSaved)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: Failed to save PNG to '%s' from actor '%s'."), *OutputFilePath, *GetName());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("RealtimeTextureBaker: Saved PNG to '%s'."), *OutputFilePath);
}

void ABakeProjectionActor::SaveProjectionCameraRenderAsPNG()
{
	if (!ProjectionCamera)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: ProjectionCamera is null on BakeProjectionActor '%s'."), *GetName());
		return;
	}

	UCameraComponent* CameraComponent = ProjectionCamera->GetCameraComponent();
	if (!CameraComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: ProjectionCamera has no CameraComponent on BakeProjectionActor '%s'."), *GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: World is null on BakeProjectionActor '%s'."), *GetName());
		return;
	}

	FMinimalViewInfo ViewInfo;
	CameraComponent->GetCameraView(0.0f, ViewInfo);
	const float AspectRatio = FMath::Max(ViewInfo.AspectRatio, 0.0001f);
	const int32 Resolution = Settings.GetClampedResolution();
	const int32 Width = Resolution;
	const int32 Height = FMath::Max(1, FMath::RoundToInt(Resolution / AspectRatio));

	UTextureRenderTarget2D* TempRenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
		World,
		Width,
		Height,
		RTF_RGBA8_SRGB,
		FLinearColor::Black,
		false
	);
	if (!TempRenderTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: Failed to create temporary render target for '%s'."), *GetName());
		return;
	}
	TempRenderTarget->TargetGamma = 2.2f;

	USceneCaptureComponent2D* TempCapture = NewObject<USceneCaptureComponent2D>(this, TEXT("ProjectionCameraCapture"));
	if (!TempCapture)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: Failed to create temporary scene capture for '%s'."), *GetName());
		return;
	}

	TempCapture->SetupAttachment(RootComponent);
	TempCapture->RegisterComponentWithWorld(World);
	TempCapture->bCaptureEveryFrame = false;
	TempCapture->bCaptureOnMovement = false;
	TempCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	TempCapture->ProjectionType = ECameraProjectionMode::Perspective;
	TempCapture->FOVAngle = CameraComponent->FieldOfView;
	TempCapture->SetWorldTransform(CameraComponent->GetComponentTransform());
	TempCapture->TextureTarget = TempRenderTarget;
	TempCapture->CaptureScene();

	FlushRenderingCommands();

	const FString OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RealtimeTextureBaker"));
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);

	const FString SafeActorName = GetName().Replace(TEXT(" "), TEXT("_"));
	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString OutputFilePath = FPaths::Combine(OutputDirectory, FString::Printf(TEXT("%s_ProjectionCamera_%s.png"), *SafeActorName, *Timestamp));

	TUniquePtr<FArchive> FileWriter(IFileManager::Get().CreateFileWriter(*OutputFilePath));
	if (!FileWriter)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: Failed to create file writer for '%s'."), *OutputFilePath);
		return;
	}

	const bool bSaved = FImageUtils::ExportRenderTarget2DAsPNG(TempRenderTarget, *FileWriter);
	FileWriter->Close();
	if (!bSaved)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: Failed to save projection camera PNG to '%s' from actor '%s'."), *OutputFilePath, *GetName());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("RealtimeTextureBaker: Saved projection camera PNG to '%s'."), *OutputFilePath);
}

namespace
{
	FVector2f ProjectWorldPositionToCameraUV(
		const FVector& WorldPosition,
		const FTransform& CameraTransform,
		const float HorizontalFovRadians,
		const float AspectRatio,
		const ECameraProjectionMode::Type ProjectionMode,
		const float OrthoWidth)
	{
		const FVector CameraSpace = CameraTransform.InverseTransformPosition(WorldPosition);

		if (ProjectionMode == ECameraProjectionMode::Orthographic)
		{
			const float SafeOrthoWidth = FMath::Max(OrthoWidth, KINDA_SMALL_NUMBER);
			const float SafeOrthoHeight = FMath::Max(SafeOrthoWidth / FMath::Max(AspectRatio, KINDA_SMALL_NUMBER), KINDA_SMALL_NUMBER);
			return FVector2f(
				0.5f + static_cast<float>(CameraSpace.Y / SafeOrthoWidth),
				0.5f - static_cast<float>(CameraSpace.Z / SafeOrthoHeight)
			);
		}

		const float TanHalfHorizontalFov = FMath::Tan(HorizontalFovRadians * 0.5f);
		const float SafeX = FMath::Abs(CameraSpace.X) < KINDA_SMALL_NUMBER
			? (CameraSpace.X >= 0.0 ? KINDA_SMALL_NUMBER : -KINDA_SMALL_NUMBER)
			: static_cast<float>(CameraSpace.X);
		const float TanHalfVerticalFov = TanHalfHorizontalFov / FMath::Max(AspectRatio, KINDA_SMALL_NUMBER);

		const float NdcX = static_cast<float>(CameraSpace.Y) / (SafeX * TanHalfHorizontalFov);
		const float NdcY = static_cast<float>(CameraSpace.Z) / (SafeX * TanHalfVerticalFov);

		return FVector2f(
			0.5f + (NdcX * 0.5f),
			0.5f - (NdcY * 0.5f)
		);
	}

	void AppendCameraProjectionUV(
		FMeshDescription& MeshDescription,
		const FTransform& MeshTransform,
		const FTransform& CameraTransform,
		const float HorizontalFovRadians,
		const float AspectRatio,
		const ECameraProjectionMode::Type ProjectionMode,
		const float OrthoWidth)
	{
	const int32 NewUVChannel = MeshDescription.GetNumUVElementChannels();
	MeshDescription.SetNumUVChannels(NewUVChannel + 1);

	FStaticMeshAttributes Attributes(MeshDescription);
	Attributes.Register();
	TMeshAttributesRef<FVertexInstanceID, FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
	if (VertexInstanceUVs.GetNumChannels() <= NewUVChannel)
	{
		UE_LOG(LogTemp, Error, TEXT("GenerateCameraProjectionUV: failed to add UV channel %d"), NewUVChannel);
		return;
	}

		for (const FVertexInstanceID VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
		{
			const FVertexID VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceID);
			const FVector WorldPosition = MeshTransform.TransformPosition(static_cast<FVector>(MeshDescription.GetVertexPosition(VertexID)));
			const FVector2f UV = ProjectWorldPositionToCameraUV(
				WorldPosition,
				CameraTransform,
				HorizontalFovRadians,
				AspectRatio,
				ProjectionMode,
				OrthoWidth
			);

			VertexInstanceUVs.Set(VertexInstanceID, NewUVChannel, UV);
		}
	}
}

void ABakeProjectionActor::GenerateCameraProjectionUV()
{
	if (!TargetMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: TargetMesh is null on BakeProjectionActor '%s'."), *GetName());
		return;
	}

	if (!ProjectionCamera)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: ProjectionCamera is null on BakeProjectionActor '%s'."), *GetName());
		return;
	}

	UStaticMeshComponent* TargetMeshComponent = TargetMesh->GetStaticMeshComponent();
	if (!TargetMeshComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: TargetMesh has no StaticMeshComponent on BakeProjectionActor '%s'."), *GetName());
		return;
	}

	UStaticMesh* SourceStaticMesh = TargetMeshComponent->GetStaticMesh();
	if (!SourceStaticMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: TargetMesh has no StaticMesh asset on BakeProjectionActor '%s'."), *GetName());
		return;
	}

	UCameraComponent* CameraComponent = ProjectionCamera->GetCameraComponent();
	if (!CameraComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: ProjectionCamera has no CameraComponent on BakeProjectionActor '%s'."), *GetName());
		return;
	}

	FMinimalViewInfo ViewInfo;
	CameraComponent->GetCameraView(0.0f, ViewInfo);
	const float AspectRatio = FMath::Max(ViewInfo.AspectRatio, 0.0001f);
	const float HorizontalFovRadians = FMath::DegreesToRadians(CameraComponent->FieldOfView);
	const FTransform CameraTransform = CameraComponent->GetComponentTransform();
	const FTransform MeshTransform = TargetMeshComponent->GetComponentTransform();

	FString UniquePackageName;
	FString UniqueAssetName;
	{
		const FString SourcePackagePath = FPackageName::GetLongPackagePath(SourceStaticMesh->GetOutermost()->GetName());
		const FString BaseAssetName = FString::Printf(TEXT("%s_CamProjUV"), *SourceStaticMesh->GetName());
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		AssetToolsModule.Get().CreateUniqueAssetName(
			FPaths::Combine(SourcePackagePath, ObjectTools::SanitizeObjectName(BaseAssetName)),
			TEXT(""),
			UniquePackageName,
			UniqueAssetName
		);
	}

	UPackage* Package = CreatePackage(*UniquePackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: Failed to create package '%s'."), *UniquePackageName);
		return;
	}

	UStaticMesh* NewStaticMesh = DuplicateObject<UStaticMesh>(SourceStaticMesh, Package, *UniqueAssetName);
	if (!NewStaticMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: Failed to duplicate StaticMesh '%s'."), *SourceStaticMesh->GetName());
		return;
	}

	NewStaticMesh->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
	NewStaticMesh->Modify();

	const int32 NumSourceModels = NewStaticMesh->GetNumSourceModels();
	bool bAnyLODUpdated = false;
	for (int32 LODIndex = 0; LODIndex < NumSourceModels; ++LODIndex)
	{
		if (!NewStaticMesh->IsMeshDescriptionValid(LODIndex))
		{
			continue;
		}

		FMeshDescription* MeshDescription = NewStaticMesh->GetMeshDescription(LODIndex);
		if (!MeshDescription)
		{
			continue;
		}

		AppendCameraProjectionUV(
			*MeshDescription,
			MeshTransform,
			CameraTransform,
			HorizontalFovRadians,
			AspectRatio,
			CameraComponent->ProjectionMode,
			CameraComponent->OrthoWidth
		);
		NewStaticMesh->CommitMeshDescription(LODIndex);
		bAnyLODUpdated = true;
	}

	if (!bAnyLODUpdated)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: No valid MeshDescription found on '%s'."), *SourceStaticMesh->GetName());
		return;
	}

	TArray<FText> BuildErrors;
	NewStaticMesh->Build(false, &BuildErrors);
	for (const FText& ErrorText : BuildErrors)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: %s"), *ErrorText.ToString());
	}

	NewStaticMesh->PostEditChange();
	NewStaticMesh->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewStaticMesh);

	const FString Filename = FPackageName::LongPackageNameToFilename(UniquePackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, NewStaticMesh, *Filename, SaveArgs))
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: Failed to save StaticMesh package '%s'."), *UniquePackageName);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("RealtimeTextureBaker: Generated camera projection UV mesh '%s'."), *UniqueAssetName);
}
#endif
