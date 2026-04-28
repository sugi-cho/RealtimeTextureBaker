#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TextureBakeTypes.h"
#include "TextureBakeSubsystem.generated.h"

class UCameraComponent;
class ACameraActor;
class UStaticMeshComponent;
class UTexture;
class UTextureRenderTarget2D;

UCLASS()
class REALTIMETEXTUREBAKER_API UTextureBakeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Realtime Texture Baker")
	UTextureRenderTarget2D* CreateBakeRenderTarget(const FRealtimeTextureBakeSettings& Settings);

	UFUNCTION(BlueprintCallable, Category = "Realtime Texture Baker")
	void ClearBakeRenderTarget(UTextureRenderTarget2D* RenderTarget, FLinearColor ClearColor);

	UTextureRenderTarget2D* GetTempRenderTarget(const FRealtimeTextureBakeSettings& Settings);

	UFUNCTION(BlueprintCallable, Category = "Realtime Texture Baker")
	bool BakeUVTextureToUV(UStaticMeshComponent* TargetMesh, UTexture* SourceTexture, UTextureRenderTarget2D* RenderTarget, const FRealtimeTextureBakeSettings& Settings);

	UFUNCTION(BlueprintCallable, Category = "Realtime Texture Baker")
	bool BakeCameraProjectionToUV(UStaticMeshComponent* TargetMesh, ACameraActor* ProjectionCamera, UTexture* SourceTexture, UTextureRenderTarget2D* RenderTarget, const FRealtimeTextureBakeSettings& Settings);

private:
	UPROPERTY(Transient)
	UTextureRenderTarget2D* TempRenderTarget;
};
