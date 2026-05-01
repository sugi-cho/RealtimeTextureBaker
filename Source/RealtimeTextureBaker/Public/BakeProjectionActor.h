#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TextureBakeTypes.h"
#include "BakeProjectionActor.generated.h"

class UCameraComponent;
class USceneCaptureComponent2D;
class ACameraActor;
class AStaticMeshActor;
class UTexture;
class UTextureRenderTarget2D;

UCLASS(Blueprintable)
class REALTIMETEXTUREBAKER_API ABakeProjectionActor : public AActor
{
	GENERATED_BODY()

public:
	ABakeProjectionActor();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	ERealtimeTextureBakeMode BakeMode = ERealtimeTextureBakeMode::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	bool bAutoBake = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	AStaticMeshActor* TargetMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	ACameraActor* ProjectionCamera = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	UTexture* SourceTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	FRealtimeTextureBakeSettings Settings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bake")
	USceneCaptureComponent2D* DepthCapture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	UTextureRenderTarget2D* DepthRenderTarget = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	UTextureRenderTarget2D* OutputRenderTarget = nullptr;

	UFUNCTION(BlueprintCallable, Category = "Realtime Texture Baker")
	UTextureRenderTarget2D* AllocateDepthRenderTarget(float AspectRatio = 1.0f);

	UFUNCTION(CallInEditor, Category = "Realtime Texture Baker", meta = (DisplayName = "AllocateDepthRenderTarget"))
	void AllocateDepthRenderTargetEditor();

	UTextureRenderTarget2D* AllocateRenderTarget();
	void UpdateDepthCaptureFromProjectionCamera();
	bool BakeUVTexture();

	UFUNCTION(BlueprintCallable, Category = "Realtime Texture Baker")
	bool BakeCameraProjection();

	UFUNCTION(CallInEditor, Category = "Realtime Texture Baker", meta = (DisplayName = "BakeCameraProjection"))
	void BakeCameraProjectionEditor();

	UFUNCTION(BlueprintCallable, Category = "Realtime Texture Baker")
	bool BakeCurrentMode();

	UFUNCTION(CallInEditor, Category = "Realtime Texture Baker", meta = (DisplayName = "BakeCurrentMode"))
	void BakeCurrentModeEditor();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Realtime Texture Baker")
	void ClearOutput();

#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Realtime Texture Baker", meta = (DisplayName = "Save Output Render Target As PNG"))
	void SaveOutputRenderTargetAsPNG();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Realtime Texture Baker", meta = (DisplayName = "Save Projection Camera Render As PNG"))
	void SaveProjectionCameraRenderAsPNG();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Realtime Texture Baker", meta = (DisplayName = "Generate Camera Projection UV"))
	void GenerateCameraProjectionUV();
#endif
};
