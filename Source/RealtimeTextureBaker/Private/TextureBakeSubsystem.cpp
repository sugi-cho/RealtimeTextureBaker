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
constexpr int32 MaxPackedFloat4sPerPass = PackedFloat4sPerTriangle;

struct FBakeTriangleData
{
	FVector4f Packed[PackedFloat4sPerTriangle];
	FIntRect Bounds = FIntRect(0, 0, 0, 0);
	float DepthSortKey = 0.0f;
};

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
		SHADER_PARAMETER(float, DepthTestThreshold)
		SHADER_PARAMETER(FVector3f, ProjectionCameraLocation)
		SHADER_PARAMETER(FVector3f, ProjectionCameraForward)
		SHADER_PARAMETER(FVector3f, ProjectionCameraRight)
		SHADER_PARAMETER(FVector3f, ProjectionCameraUp)
		SHADER_PARAMETER(FLinearColor, ClearColor)
		SHADER_PARAMETER_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, DepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DepthSampler)
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
		SHADER_PARAMETER_TEXTURE(Texture2D, MaskTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, MaskSampler)
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

static bool BuildBakeTriangles(
	UStaticMeshComponent* TargetMeshComponent,
	const int32 TargetUVChannel,
	const int32 SourceUVChannel,
	const FIntPoint& OutputExtent,
	const bool bSortByDepth,
	const FVector3f& CameraLocation,
	const FVector3f& CameraForward,
	TArray<FBakeTriangleData>& OutTriangles)
{
	OutTriangles.Reset();

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

	OutTriangles.Reserve(TotalTriangles);

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

			FBakeTriangleData& Triangle = OutTriangles.AddDefaulted_GetRef();
			Triangle.Packed[0] = FVector4f(TargetUV0.X, TargetUV0.Y, SourceUV0.X, SourceUV0.Y);
			Triangle.Packed[1] = FVector4f(TargetUV1.X, TargetUV1.Y, SourceUV1.X, SourceUV1.Y);
			Triangle.Packed[2] = FVector4f(TargetUV2.X, TargetUV2.Y, SourceUV2.X, SourceUV2.Y);
			Triangle.Packed[3] = FVector4f(P0, 0.0f);
			Triangle.Packed[4] = FVector4f(P1, 0.0f);
			Triangle.Packed[5] = FVector4f(P2, 0.0f);

			const float MinU = FMath::Min3(TargetUV0.X, TargetUV1.X, TargetUV2.X);
			const float MinV = FMath::Min3(TargetUV0.Y, TargetUV1.Y, TargetUV2.Y);
			const float MaxU = FMath::Max3(TargetUV0.X, TargetUV1.X, TargetUV2.X);
			const float MaxV = FMath::Max3(TargetUV0.Y, TargetUV1.Y, TargetUV2.Y);

			const int32 MinX = FMath::Clamp(FMath::FloorToInt(MinU * OutputExtent.X), 0, FMath::Max(0, OutputExtent.X - 1));
			const int32 MinY = FMath::Clamp(FMath::FloorToInt(MinV * OutputExtent.Y), 0, FMath::Max(0, OutputExtent.Y - 1));
			const int32 MaxX = FMath::Clamp(FMath::CeilToInt(MaxU * OutputExtent.X), MinX + 1, OutputExtent.X);
			const int32 MaxY = FMath::Clamp(FMath::CeilToInt(MaxV * OutputExtent.Y), MinY + 1, OutputExtent.Y);
			Triangle.Bounds = FIntRect(FIntPoint(MinX, MinY), FIntPoint(MaxX, MaxY));
			Triangle.DepthSortKey = bSortByDepth ? FVector3f::DotProduct(((P0 + P1 + P2) / 3.0f) - CameraLocation, CameraForward) : 0.0f;
		}
	}

	OutTriangles.RemoveAllSwap([](const FBakeTriangleData& Triangle)
	{
		return Triangle.Bounds.Min.X >= Triangle.Bounds.Max.X || Triangle.Bounds.Min.Y >= Triangle.Bounds.Max.Y;
	});

	if (bSortByDepth)
	{
		OutTriangles.Sort([](const FBakeTriangleData& A, const FBakeTriangleData& B)
		{
			return A.DepthSortKey > B.DepthSortKey;
		});
	}

	return OutTriangles.Num() > 0;
}

static bool DispatchRasterBakePass(
	UWorld* World,
	UTextureRenderTarget2D* RenderTarget,
	UTexture* SourceTexture,
	UTextureRenderTarget2D* DepthRenderTarget,
	const TArray<FBakeTriangleData>& Triangles,
	int32 BakeMode,
	const FRealtimeTextureBakeSettings& Settings,
	const FVector3f& CameraLocation,
	const FVector3f& CameraForward,
	const FVector3f& CameraRight,
	const FVector3f& CameraUp,
	const float CameraFOV,
	const float CameraAspectRatio)
{
	if (!World || !RenderTarget || !SourceTexture || Triangles.Num() == 0)
	{
		return false;
	}

	FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
	FTextureResource* SourceResource = SourceTexture->GetResource();
	FTextureRenderTargetResource* DepthRenderTargetResource = DepthRenderTarget ? DepthRenderTarget->GameThread_GetRenderTargetResource() : nullptr;
	if (!RenderTargetResource || !SourceResource || !SourceResource->TextureRHI)
	{
		return false;
	}

	FRHITexture* OutputTexture = RenderTargetResource->GetRenderTargetTexture();
	FRHITexture* InputTexture = SourceResource->TextureRHI;
	FRHITexture* DepthTexture = DepthRenderTargetResource ? DepthRenderTargetResource->GetRenderTargetTexture() : nullptr;
	if (!OutputTexture || !InputTexture)
	{
		return false;
	}

	if (BakeMode == 1 && Settings.bUseDepthTest && !DepthTexture)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTextureBaker: DepthRenderTarget is invalid while depth testing is enabled."));
		return false;
	}

	if (!DepthTexture)
	{
		DepthTexture = InputTexture;
	}

	FRHISamplerState* SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	const FIntPoint OutputExtent(RenderTarget->SizeX, RenderTarget->SizeY);
	const FIntRect OutputRect(FIntPoint::ZeroValue, OutputExtent);
	const FScreenPassTextureViewport OutputViewport(OutputExtent, OutputRect);
	const FScreenPassTextureViewport InputViewport(OutputExtent, OutputRect);
	const FScreenPassViewInfo ViewInfo;
	TArray<FBakeTriangleData> TrianglePayload = Triangles;

	ENQUEUE_RENDER_COMMAND(RealtimeTextureBakerRender)(
		[OutputTexture, InputTexture, DepthTexture, SourceSampler, OutputViewport, InputViewport, ViewInfo, TrianglePayload = MoveTemp(TrianglePayload), BakeMode, Settings, CameraLocation, CameraForward, CameraRight, CameraUp, CameraFOV, CameraAspectRatio](FRHICommandListImmediate& RHICmdList)
		{
			FRHIRenderPassInfo RenderPassInfo(OutputTexture, ERenderTargetActions::Load_Store);
			RHICmdList.BeginRenderPass(RenderPassInfo, TEXT("RealtimeTextureBaker"));

			TShaderMapRef<FScreenPassVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			TShaderMapRef<FBakeProjectionPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FScreenPassPipelineState PipelineState(VertexShader, PixelShader);

			for (const FBakeTriangleData& Triangle : TrianglePayload)
			{
				const FIntRect& Bounds = Triangle.Bounds;
				if (Bounds.Min.X >= Bounds.Max.X || Bounds.Min.Y >= Bounds.Max.Y)
				{
					continue;
				}

				FBakeProjectionPS::FParameters Parameters = {};
				for (int32 Index = 0; Index < PackedFloat4sPerTriangle; ++Index)
				{
					Parameters.TriangleData[Index] = Triangle.Packed[Index];
				}
				Parameters.TriangleCount = 1;
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
				Parameters.DepthTexture = DepthTexture;
				Parameters.DepthSampler = SourceSampler;
				Parameters.DepthTestThreshold = Settings.DepthTestThreshold;

				RHICmdList.SetScissorRect(
					true,
					Bounds.Min.X,
					Bounds.Min.Y,
					Bounds.Max.X,
					Bounds.Max.Y);

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

			RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
			RHICmdList.EndRenderPass();
		});

	FlushRenderingCommands();
	return true;
}

static bool DispatchDilationPass(
	UWorld* World,
	UTextureRenderTarget2D* SourceRenderTarget,
	UTextureRenderTarget2D* DestRenderTarget,
	UTextureRenderTarget2D* MaskRenderTarget,
	const FRealtimeTextureBakeSettings& Settings)
{
	if (!World || !SourceRenderTarget || !DestRenderTarget || Settings.DilationPixels <= 0)
	{
		return false;
	}

	FTextureRenderTargetResource* SourceResource = SourceRenderTarget->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* DestResource = DestRenderTarget->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* MaskResource = MaskRenderTarget ? MaskRenderTarget->GameThread_GetRenderTargetResource() : nullptr;
	if (!SourceResource || !DestResource || !MaskResource)
	{
		return false;
	}

	FRHITexture* SourceTexture = SourceResource->GetRenderTargetTexture();
	FRHITexture* DestTexture = DestResource->GetRenderTargetTexture();
	FRHITexture* MaskTextureRHI = MaskResource->GetRenderTargetTexture();
	if (!SourceTexture || !DestTexture || !MaskTextureRHI)
	{
		return false;
	}

	FRHISamplerState* SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	const FIntPoint Extent(DestRenderTarget->SizeX, DestRenderTarget->SizeY);
	const FIntRect Rect(FIntPoint::ZeroValue, Extent);
	const FScreenPassTextureViewport OutputViewport(Extent, Rect);
	const FScreenPassTextureViewport InputViewport(Extent, Rect);
	const FScreenPassViewInfo ViewInfo;
	const FVector2f SourceTexelSize(
		1.0f / FMath::Max(1, Extent.X),
		1.0f / FMath::Max(1, Extent.Y));

	ENQUEUE_RENDER_COMMAND(RealtimeTextureBakerDilate)(
		[SourceTexture, DestTexture, MaskTextureRHI, SourceSampler, OutputViewport, InputViewport, ViewInfo, Settings, SourceTexelSize](FRHICommandListImmediate& RHICmdList)
		{
			FRHIRenderPassInfo RenderPassInfo(DestTexture, ERenderTargetActions::Load_Store);
			RHICmdList.BeginRenderPass(RenderPassInfo, TEXT("RealtimeTextureBakerDilate"));

			TShaderMapRef<FScreenPassVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			TShaderMapRef<FDilateProjectionPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FScreenPassPipelineState PipelineState(VertexShader, PixelShader);

			FDilateProjectionPS::FParameters Parameters = {};
			Parameters.SourceTexture = SourceTexture;
			Parameters.SourceSampler = SourceSampler;
			Parameters.MaskTexture = MaskTextureRHI;
			Parameters.MaskSampler = SourceSampler;
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

UTextureRenderTarget2D* UTextureBakeSubsystem::GetMaskRenderTarget(const FRealtimeTextureBakeSettings& Settings)
{
	const int32 Resolution = Settings.GetClampedResolution();
	if (MaskRenderTarget)
	{
		if (MaskRenderTarget->SizeX != Resolution || MaskRenderTarget->SizeY != Resolution || MaskRenderTarget->RenderTargetFormat != Settings.RenderTargetFormat)
		{
			MaskRenderTarget = nullptr;
		}
	}

	if (!MaskRenderTarget)
	{
		MaskRenderTarget = CreateBakeRenderTarget(Settings);
	}
	return MaskRenderTarget;
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
	if (!RenderTarget)
	{
		return false;
	}

	TArray<FBakeTriangleData> Triangles;
	const FIntPoint OutputExtent(RenderTarget->SizeX, RenderTarget->SizeY);
	if (!BuildBakeTriangles(
		TargetMesh,
		Settings.GetClampedTargetUVChannel(),
		Settings.GetClampedSourceUVChannel(),
		OutputExtent,
		false,
		FVector3f::ZeroVector,
		FVector3f::ForwardVector,
		Triangles))
	{
		return false;
	}
	UWorld* World = GetWorld();

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

		UTextureRenderTarget2D* LocalMaskRenderTarget = GetMaskRenderTarget(Settings);
		if (!LocalMaskRenderTarget)
		{
			return false;
		}

		ClearBakeRenderTarget(LocalTempRenderTarget, Settings.ClearColor);
		ClearBakeRenderTarget(LocalMaskRenderTarget, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));

		if (!DispatchRasterBakePass(World, LocalMaskRenderTarget, SourceTexture, nullptr, Triangles, 2, Settings, FVector3f::ZeroVector, FVector3f::ForwardVector, FVector3f::RightVector, FVector3f::UpVector, 0.0f, 1.0f))
		{
			return false;
		}

		if (!DispatchRasterBakePass(World, LocalTempRenderTarget, SourceTexture, nullptr, Triangles, 0, Settings, FVector3f::ZeroVector, FVector3f::ForwardVector, FVector3f::RightVector, FVector3f::UpVector, 0.0f, 1.0f))
		{
			return false;
		}

		return DispatchDilationPass(World, LocalTempRenderTarget, RenderTarget, LocalMaskRenderTarget, Settings);
	}

	return DispatchRasterBakePass(World, RenderTarget, SourceTexture, nullptr, Triangles, 0, Settings, FVector3f::ZeroVector, FVector3f::ForwardVector, FVector3f::RightVector, FVector3f::UpVector, 0.0f, 1.0f);
}

bool UTextureBakeSubsystem::BakeCameraProjectionToUV(UStaticMeshComponent* TargetMesh, ACameraActor* ProjectionCamera, UTexture* SourceTexture, UTextureRenderTarget2D* RenderTarget, UTextureRenderTarget2D* DepthRenderTarget, const FRealtimeTextureBakeSettings& Settings)
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

	FMinimalViewInfo ViewInfo;
	CameraComponent->GetCameraView(0.0f, ViewInfo);
	const float CameraFOV = ViewInfo.FOV;
	const float CameraAspectRatio = FMath::Max(ViewInfo.AspectRatio, 0.0001f);
	const FTransform CameraTransform = CameraComponent->GetComponentTransform();

	const FVector3f CameraLocation = FVector3f(CameraTransform.GetLocation());
	const FVector3f CameraForward = FVector3f(CameraTransform.GetUnitAxis(EAxis::X));
	const FVector3f CameraRight = FVector3f(CameraTransform.GetUnitAxis(EAxis::Y));
	const FVector3f CameraUp = FVector3f(CameraTransform.GetUnitAxis(EAxis::Z));

	if (!RenderTarget)
	{
		return false;
	}

	TArray<FBakeTriangleData> Triangles;
	const FIntPoint OutputExtent(RenderTarget->SizeX, RenderTarget->SizeY);
	if (!BuildBakeTriangles(
		TargetMesh,
		Settings.GetClampedTargetUVChannel(),
		Settings.GetClampedSourceUVChannel(),
		OutputExtent,
		true,
		CameraLocation,
		CameraForward,
		Triangles))
	{
		return false;
	}

	UWorld* World = GetWorld();

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

		UTextureRenderTarget2D* LocalMaskRenderTarget = GetMaskRenderTarget(Settings);
		if (!LocalMaskRenderTarget)
		{
			return false;
		}

		ClearBakeRenderTarget(LocalTempRenderTarget, Settings.ClearColor);
		ClearBakeRenderTarget(LocalMaskRenderTarget, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));

		if (!DispatchRasterBakePass(World, LocalMaskRenderTarget, SourceTexture, nullptr, Triangles, 2, Settings, CameraLocation, CameraForward, CameraRight, CameraUp, CameraFOV, CameraAspectRatio))
		{
			return false;
		}

		if (!DispatchRasterBakePass(World, LocalTempRenderTarget, SourceTexture, DepthRenderTarget, Triangles, 1, Settings, CameraLocation, CameraForward, CameraRight, CameraUp, CameraFOV, CameraAspectRatio))
		{
			return false;
		}

		return DispatchDilationPass(World, LocalTempRenderTarget, RenderTarget, LocalMaskRenderTarget, Settings);
	}

	return DispatchRasterBakePass(World, RenderTarget, SourceTexture, DepthRenderTarget, Triangles, 1, Settings, CameraLocation, CameraForward, CameraRight, CameraUp, CameraFOV, CameraAspectRatio);
}
