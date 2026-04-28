#include "TextureBakeSubsystem.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "GlobalShader.h"
#include "Misc/Paths.h"
#include "RHICommandList.h"
#include "RenderResource.h"
#include "RHIStaticStates.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"
#include "StaticMeshResources.h"
#include "TextureResource.h"

namespace
{
constexpr int32 PackedFloat4sPerTriangle = 6;
constexpr int32 MaxBakeTrianglesPerPass = 512;
constexpr int32 MaxPackedFloat4sPerPass = MaxBakeTrianglesPerPass * PackedFloat4sPerTriangle;

class FBakeProjectionPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FBakeProjectionPS);
	SHADER_USE_PARAMETER_STRUCT(FBakeProjectionPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_ARRAY(FVector4f, TriangleData, [MaxPackedFloat4sPerPass])
		SHADER_PARAMETER(int32, TriangleCount)
		SHADER_PARAMETER(int32, BakeMode)
		SHADER_PARAMETER(int32, UseDepthTest)
		SHADER_PARAMETER(int32, UseNormalWeight)
		SHADER_PARAMETER(float, ProjectionCameraFOV)
		SHADER_PARAMETER(float, ProjectionCameraAspectRatio)
		SHADER_PARAMETER(FVector3f, ProjectionCameraLocation)
		SHADER_PARAMETER(FVector3f, ProjectionCameraForward)
		SHADER_PARAMETER(FVector3f, ProjectionCameraRight)
		SHADER_PARAMETER(FVector3f, ProjectionCameraUp)
		SHADER_PARAMETER(FLinearColor, ClearColor)
		SHADER_PARAMETER_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&)
	{
		return true;
	}
};

IMPLEMENT_GLOBAL_SHADER(FBakeProjectionPS, "/RealtimeTextureBaker/RealtimeTextureBaker.usf", "MainPS", SF_Pixel);

class FDilateProjectionPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDilateProjectionPS);
	SHADER_USE_PARAMETER_STRUCT(FDilateProjectionPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
		SHADER_PARAMETER(float, DilationPixels)
		SHADER_PARAMETER(FVector2f, SourceTexelSize)
		SHADER_PARAMETER(FLinearColor, ClearColor)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&)
	{
		return true;
	}
};

IMPLEMENT_GLOBAL_SHADER(FDilateProjectionPS, "/RealtimeTextureBaker/RealtimeTextureBaker.usf", "DilatePS", SF_Pixel);

static bool ExtractBakeTriangles(
	UStaticMeshComponent* TargetMeshComponent,
	const int32 TargetUVChannel,
	const int32 SourceUVChannel,
	TArray<FVector4f>& OutPackedTriangleData,
	int32& OutTriangleCount)
{
	OutPackedTriangleData.Reset();
	OutTriangleCount = 0;

	if (!TargetMeshComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: TargetMeshComponent is null."));
		return false;
	}

	UStaticMesh* StaticMesh = TargetMeshComponent->GetStaticMesh();
	if (!StaticMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: Target mesh component has no StaticMesh."));
		return false;
	}

	const FStaticMeshRenderData* RenderData = StaticMesh->GetRenderData();
	if (!RenderData || RenderData->LODResources.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: StaticMesh '%s' has no render data."), *StaticMesh->GetName());
		return false;
	}

	const FStaticMeshLODResources& LODResources = RenderData->LODResources[0];
	if (LODResources.Sections.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: StaticMesh '%s' has no sections."), *StaticMesh->GetName());
		return false;
	}

	const FIndexArrayView Indices = LODResources.IndexBuffer.GetArrayView();
	const FStaticMeshVertexBuffer& VertexBuffer = LODResources.VertexBuffers.StaticMeshVertexBuffer;
	const FPositionVertexBuffer& PositionBuffer = LODResources.VertexBuffers.PositionVertexBuffer;
	const FTransform ComponentTransform = TargetMeshComponent->GetComponentTransform();

	const int32 NumTexCoords = static_cast<int32>(VertexBuffer.GetNumTexCoords());
	if (TargetUVChannel >= NumTexCoords || SourceUVChannel >= NumTexCoords)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: StaticMesh '%s' has %d UV channels. Requested TargetUV=%d SourceUV=%d."),
			*StaticMesh->GetName(), NumTexCoords, TargetUVChannel, SourceUVChannel);
		return false;
	}

	const int32 TotalTriangles = Indices.Num() / 3;
	if (TotalTriangles <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: StaticMesh '%s' has no triangles."), *StaticMesh->GetName());
		return false;
	}

	OutPackedTriangleData.Reserve(TotalTriangles * PackedFloat4sPerTriangle);

	for (const FStaticMeshSection& Section : LODResources.Sections)
	{
		for (int32 TriangleIndex = 0; TriangleIndex < static_cast<int32>(Section.NumTriangles); ++TriangleIndex)
		{
			const int32 BaseIndex = Section.FirstIndex + TriangleIndex * 3;
			if (BaseIndex + 2 >= Indices.Num())
			{
				return false;
			}

			const uint32 I0 = Indices[BaseIndex + 0];
			const uint32 I1 = Indices[BaseIndex + 1];
			const uint32 I2 = Indices[BaseIndex + 2];

			const FVector3f P0 = FVector3f(ComponentTransform.TransformPosition(FVector3d(PositionBuffer.VertexPosition(I0))));
			const FVector3f P1 = FVector3f(ComponentTransform.TransformPosition(FVector3d(PositionBuffer.VertexPosition(I1))));
			const FVector3f P2 = FVector3f(ComponentTransform.TransformPosition(FVector3d(PositionBuffer.VertexPosition(I2))));

			const FVector2f TargetUV0 = VertexBuffer.GetVertexUV(I0, TargetUVChannel);
			const FVector2f TargetUV1 = VertexBuffer.GetVertexUV(I1, TargetUVChannel);
			const FVector2f TargetUV2 = VertexBuffer.GetVertexUV(I2, TargetUVChannel);

			const FVector2f SourceUV0 = VertexBuffer.GetVertexUV(I0, SourceUVChannel);
			const FVector2f SourceUV1 = VertexBuffer.GetVertexUV(I1, SourceUVChannel);
			const FVector2f SourceUV2 = VertexBuffer.GetVertexUV(I2, SourceUVChannel);

			OutPackedTriangleData.Add(FVector4f(TargetUV0.X, TargetUV0.Y, SourceUV0.X, SourceUV0.Y));
			OutPackedTriangleData.Add(FVector4f(TargetUV1.X, TargetUV1.Y, SourceUV1.X, SourceUV1.Y));
			OutPackedTriangleData.Add(FVector4f(TargetUV2.X, TargetUV2.Y, SourceUV2.X, SourceUV2.Y));
			OutPackedTriangleData.Add(FVector4f(P0, 0.0f));
			OutPackedTriangleData.Add(FVector4f(P1, 0.0f));
			OutPackedTriangleData.Add(FVector4f(P2, 0.0f));

			++OutTriangleCount;
		}
	}

	return OutTriangleCount > 0;
}

static bool DispatchBakePass(
	UWorld* World,
	UTextureRenderTarget2D* RenderTarget,
	UTexture* SourceTexture,
	const TArray<FVector4f>& PackedTriangleData,
	int32 TriangleCount,
	int32 BakeMode,
	const FRealtimeTextureBakeSettings& Settings,
	const FVector3f& CameraLocation,
	const FVector3f& CameraForward,
	const FVector3f& CameraRight,
	const FVector3f& CameraUp,
	const float CameraFOV,
	const float CameraAspectRatio)
{
	if (!World || !RenderTarget || !SourceTexture || TriangleCount <= 0 || PackedTriangleData.Num() == 0)
	{
		return false;
	}

	FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
	FTextureResource* SourceResource = SourceTexture->GetResource();
	if (!RenderTargetResource || !SourceResource || !SourceResource->TextureRHI)
	{
		return false;
	}

	FRHITexture* OutputTexture = RenderTargetResource->GetRenderTargetTexture();
	FRHITexture* InputTexture = SourceResource->TextureRHI;
	if (!OutputTexture || !InputTexture)
	{
		return false;
	}

	FRHISamplerState* SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	const FIntPoint OutputExtent(RenderTarget->SizeX, RenderTarget->SizeY);
	const FIntRect OutputRect(FIntPoint::ZeroValue, OutputExtent);
	const FScreenPassTextureViewport OutputViewport(OutputExtent, OutputRect);
	const FScreenPassTextureViewport InputViewport(OutputExtent, OutputRect);
	const FScreenPassViewInfo ViewInfo(GMaxRHIFeatureLevel);

	TArray<FVector4f> TrianglePayload = PackedTriangleData;

	ENQUEUE_RENDER_COMMAND(RealtimeTextureBakerRender)(
		[OutputTexture, InputTexture, SourceSampler, OutputViewport, InputViewport, ViewInfo, TrianglePayload = MoveTemp(TrianglePayload), TriangleCount, BakeMode, Settings, CameraLocation, CameraForward, CameraRight, CameraUp, CameraFOV, CameraAspectRatio](FRHICommandListImmediate& RHICmdList)
		{
			FRHIRenderPassInfo RenderPassInfo(OutputTexture, ERenderTargetActions::Load_Store);
			RHICmdList.BeginRenderPass(RenderPassInfo, TEXT("RealtimeTextureBaker"));

			TShaderMapRef<FScreenPassVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			TShaderMapRef<FBakeProjectionPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FScreenPassPipelineState PipelineState(VertexShader, PixelShader);

			for (int32 BatchTriangleStart = 0; BatchTriangleStart < TriangleCount; BatchTriangleStart += MaxBakeTrianglesPerPass)
			{
				const int32 BatchTriangleCount = FMath::Min(MaxBakeTrianglesPerPass, TriangleCount - BatchTriangleStart);
				const int32 SourceFloat4Offset = BatchTriangleStart * PackedFloat4sPerTriangle;
				const int32 BatchFloat4Count = BatchTriangleCount * PackedFloat4sPerTriangle;

				FBakeProjectionPS::FParameters Parameters = {};
				for (int32 Index = 0; Index < BatchFloat4Count; ++Index)
				{
					Parameters.TriangleData[Index] = TrianglePayload[SourceFloat4Offset + Index];
				}
				Parameters.TriangleCount = BatchTriangleCount;
				Parameters.BakeMode = BakeMode;
				Parameters.UseDepthTest = (BakeMode == 1 && Settings.bUseDepthTest) ? 1 : 0;
				Parameters.UseNormalWeight = (BakeMode == 1 && Settings.bUseNormalWeight) ? 1 : 0;
				Parameters.ProjectionCameraFOV = CameraFOV;
				Parameters.ProjectionCameraAspectRatio = CameraAspectRatio;
				Parameters.ProjectionCameraLocation = CameraLocation;
				Parameters.ProjectionCameraForward = CameraForward;
				Parameters.ProjectionCameraRight = CameraRight;
				Parameters.ProjectionCameraUp = CameraUp;
				Parameters.ClearColor = Settings.ClearColor;
				Parameters.SourceTexture = InputTexture;
				Parameters.SourceSampler = SourceSampler;

				DrawScreenPass(
					RHICmdList,
					ViewInfo,
					OutputViewport,
					InputViewport,
					PipelineState,
					EScreenPassDrawFlags::None,
					[&](FRHICommandList& RHICmdListInner)
					{
						SetShaderParameters(RHICmdListInner, PixelShader, PixelShader.GetPixelShader(), Parameters);
					});
			}

			RHICmdList.EndRenderPass();
		});

	FlushRenderingCommands();
	return true;
}

static bool DispatchDilationPass(
	UWorld* World,
	UTextureRenderTarget2D* SourceRenderTarget,
	UTextureRenderTarget2D* DestRenderTarget,
	const FRealtimeTextureBakeSettings& Settings)
{
	if (!World || !SourceRenderTarget || !DestRenderTarget || Settings.DilationPixels <= 0)
	{
		return false;
	}

	FTextureRenderTargetResource* SourceResource = SourceRenderTarget->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* DestResource = DestRenderTarget->GameThread_GetRenderTargetResource();
	if (!SourceResource || !DestResource)
	{
		return false;
	}

	FRHITexture* SourceTexture = SourceResource->GetRenderTargetTexture();
	FRHITexture* DestTexture = DestResource->GetRenderTargetTexture();
	if (!SourceTexture || !DestTexture)
	{
		return false;
	}

	FRHISamplerState* SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	const FIntPoint Extent(DestRenderTarget->SizeX, DestRenderTarget->SizeY);
	const FIntRect Rect(FIntPoint::ZeroValue, Extent);
	const FScreenPassTextureViewport OutputViewport(Extent, Rect);
	const FScreenPassTextureViewport InputViewport(Extent, Rect);
	const FScreenPassViewInfo ViewInfo(GMaxRHIFeatureLevel);
	const FVector2f SourceTexelSize(
		1.0f / FMath::Max(1, Extent.X),
		1.0f / FMath::Max(1, Extent.Y));

	ENQUEUE_RENDER_COMMAND(RealtimeTextureBakerDilate)(
		[SourceTexture, DestTexture, SourceSampler, OutputViewport, InputViewport, ViewInfo, Settings, SourceTexelSize](FRHICommandListImmediate& RHICmdList)
		{
			FRHIRenderPassInfo RenderPassInfo(DestTexture, ERenderTargetActions::Load_Store);
			RHICmdList.BeginRenderPass(RenderPassInfo, TEXT("RealtimeTextureBakerDilate"));

			TShaderMapRef<FScreenPassVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			TShaderMapRef<FDilateProjectionPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FScreenPassPipelineState PipelineState(VertexShader, PixelShader);

			FDilateProjectionPS::FParameters Parameters = {};
			Parameters.SourceTexture = SourceTexture;
			Parameters.SourceSampler = SourceSampler;
			Parameters.DilationPixels = static_cast<float>(Settings.DilationPixels);
			Parameters.SourceTexelSize = SourceTexelSize;
			Parameters.ClearColor = Settings.ClearColor;

			DrawScreenPass(
				RHICmdList,
				ViewInfo,
				OutputViewport,
				InputViewport,
				PipelineState,
				EScreenPassDrawFlags::None,
				[&](FRHICommandList& RHICmdListInner)
				{
					SetShaderParameters(RHICmdListInner, PixelShader, PixelShader.GetPixelShader(), Parameters);
				});

			RHICmdList.EndRenderPass();
		});

	FlushRenderingCommands();
	return true;
}
} // namespace

UTextureRenderTarget2D* UTextureBakeSubsystem::GetTempRenderTarget(const FRealtimeTextureBakeSettings& Settings)
{
	const int32 Resolution = Settings.GetClampedResolution();
	if (TempRenderTarget)
	{
		if (TempRenderTarget->SizeX != Resolution || TempRenderTarget->SizeY != Resolution || TempRenderTarget->RenderTargetFormat != Settings.RenderTargetFormat)
		{
			TempRenderTarget = nullptr; // Let GC handle the old one
		}
	}

	if (!TempRenderTarget)
	{
		TempRenderTarget = CreateBakeRenderTarget(Settings);
	}
	return TempRenderTarget;
}

UTextureRenderTarget2D* UTextureBakeSubsystem::CreateBakeRenderTarget(const FRealtimeTextureBakeSettings& Settings)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const int32 Resolution = Settings.GetClampedResolution();
	return UKismetRenderingLibrary::CreateRenderTarget2D(
		World,
		Resolution,
		Resolution,
		Settings.RenderTargetFormat,
		Settings.ClearColor,
		false
	);
}

void UTextureBakeSubsystem::ClearBakeRenderTarget(UTextureRenderTarget2D* RenderTarget, FLinearColor ClearColor)
{
	if (UWorld* World = GetWorld(); World && RenderTarget)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(World, RenderTarget, ClearColor);
	}
}

bool UTextureBakeSubsystem::BakeUVTextureToUV(UStaticMeshComponent* TargetMesh, UTexture* SourceTexture, UTextureRenderTarget2D* RenderTarget, const FRealtimeTextureBakeSettings& Settings)
{
	TArray<FVector4f> PackedTriangleData;
	int32 TriangleCount = 0;
	if (!ExtractBakeTriangles(TargetMesh, Settings.GetClampedTargetUVChannel(), Settings.GetClampedSourceUVChannel(), PackedTriangleData, TriangleCount))
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!RenderTarget)
	{
		return false;
	}

	if (Settings.bAutoClear)
	{
		ClearBakeRenderTarget(RenderTarget, Settings.ClearColor);
	}

	if (Settings.DilationPixels > 0)
	{
		UTextureRenderTarget2D* LocalTempRenderTarget = GetTempRenderTarget(Settings);
		if (!LocalTempRenderTarget)
		{
			return false;
		}

		ClearBakeRenderTarget(LocalTempRenderTarget, Settings.ClearColor);

		if (!DispatchBakePass(World, LocalTempRenderTarget, SourceTexture, PackedTriangleData, TriangleCount, 0, Settings, FVector3f::ZeroVector, FVector3f::ForwardVector, FVector3f::RightVector, FVector3f::UpVector, 0.0f, 1.0f))
		{
			return false;
		}

		return DispatchDilationPass(World, LocalTempRenderTarget, RenderTarget, Settings);
	}

	return DispatchBakePass(World, RenderTarget, SourceTexture, PackedTriangleData, TriangleCount, 0, Settings, FVector3f::ZeroVector, FVector3f::ForwardVector, FVector3f::RightVector, FVector3f::UpVector, 0.0f, 1.0f);
}

bool UTextureBakeSubsystem::BakeCameraProjectionToUV(UStaticMeshComponent* TargetMesh, ACameraActor* ProjectionCamera, UTexture* SourceTexture, UTextureRenderTarget2D* RenderTarget, const FRealtimeTextureBakeSettings& Settings)
{
	if (!ProjectionCamera)
	{
		return false;
	}

	UCameraComponent* CameraComponent = ProjectionCamera->GetCameraComponent();
	if (!CameraComponent)
	{
		return false;
	}

	TArray<FVector4f> PackedTriangleData;
	int32 TriangleCount = 0;
	if (!ExtractBakeTriangles(TargetMesh, Settings.GetClampedTargetUVChannel(), Settings.GetClampedSourceUVChannel(), PackedTriangleData, TriangleCount))
	{
		return false;
	}

	FMinimalViewInfo ViewInfo;
	CameraComponent->GetCameraView(0.0f, ViewInfo);
	const float CameraFOV = ViewInfo.FOV;
	const float CameraAspectRatio = FMath::Max(ViewInfo.AspectRatio, 0.0001f);
	const FTransform CameraTransform = CameraComponent->GetComponentTransform();

	const FVector3f CameraLocation = FVector3f(CameraTransform.GetLocation());
	const FVector3f CameraForward = FVector3f(CameraTransform.GetUnitAxis(EAxis::X));
	const FVector3f CameraRight = FVector3f(CameraTransform.GetUnitAxis(EAxis::Y));
	const FVector3f CameraUp = FVector3f(CameraTransform.GetUnitAxis(EAxis::Z));

	UWorld* World = GetWorld();
	if (!RenderTarget)
	{
		return false;
	}

	if (Settings.bAutoClear)
	{
		ClearBakeRenderTarget(RenderTarget, Settings.ClearColor);
	}

	if (Settings.DilationPixels > 0)
	{
		UTextureRenderTarget2D* LocalTempRenderTarget = GetTempRenderTarget(Settings);
		if (!LocalTempRenderTarget)
		{
			return false;
		}

		ClearBakeRenderTarget(LocalTempRenderTarget, Settings.ClearColor);

		if (!DispatchBakePass(World, LocalTempRenderTarget, SourceTexture, PackedTriangleData, TriangleCount, 1, Settings, CameraLocation, CameraForward, CameraRight, CameraUp, CameraFOV, CameraAspectRatio))
		{
			return false;
		}

		return DispatchDilationPass(World, LocalTempRenderTarget, RenderTarget, Settings);
	}

	return DispatchBakePass(World, RenderTarget, SourceTexture, PackedTriangleData, TriangleCount, 1, Settings, CameraLocation, CameraForward, CameraRight, CameraUp, CameraFOV, CameraAspectRatio);
}
