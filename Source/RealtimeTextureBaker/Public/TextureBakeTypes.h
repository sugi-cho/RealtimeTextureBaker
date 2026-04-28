#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureBakeTypes.generated.h"

UENUM(BlueprintType)
enum class ERealtimeTextureBakeSource : uint8
{
	UVTexture UMETA(DisplayName = "UV Texture"),
	Camera UMETA(DisplayName = "Camera"),
	RenderTarget UMETA(DisplayName = "Render Target")
};

UENUM(BlueprintType)
enum class ERealtimeTextureBakeMode : uint8
{
	None UMETA(DisplayName = "None"),
	BakeUV UMETA(DisplayName = "Bake UV"),
	BakeCamera UMETA(DisplayName = "Bake Camera")
};

USTRUCT(BlueprintType)
struct REALTIMETEXTUREBAKER_API FRealtimeTextureBakeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake", meta = (ClampMin = "0", ClampMax = "7"))
	int32 TargetUVChannel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake", meta = (ClampMin = "0", ClampMax = "7"))
	int32 SourceUVChannel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake", meta = (ClampMin = "64", ClampMax = "8192"))
	int32 Resolution = 1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	TEnumAsByte<ETextureRenderTargetFormat> RenderTargetFormat = RTF_RGBA8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	FLinearColor ClearColor = FLinearColor::Transparent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	bool bAutoClear = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
	bool bUseDepthTest = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
	bool bUseNormalWeight = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Process", meta = (ClampMin = "0", ClampMax = "32"))
	int32 DilationPixels = 4;

	int32 GetClampedTargetUVChannel() const { return FMath::Clamp(TargetUVChannel, 0, 7); }
	int32 GetClampedSourceUVChannel() const { return FMath::Clamp(SourceUVChannel, 0, 7); }
	int32 GetClampedResolution() const { return FMath::Clamp(Resolution, 64, 8192); }
};
