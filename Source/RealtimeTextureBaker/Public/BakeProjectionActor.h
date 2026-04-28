#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TextureBakeTypes.h"
#include "BakeProjectionActor.generated.h"

class UCameraComponent;
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

#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	ERealtimeTextureBakeMode BakeMode = ERealtimeTextureBakeMode::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	AStaticMeshActor* TargetMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	ACameraActor* ProjectionCamera = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	UTexture* SourceTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	FRealtimeTextureBakeSettings Settings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	UTextureRenderTarget2D* OutputRenderTarget = nullptr;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Realtime Texture Baker")
	UTextureRenderTarget2D* AllocateRenderTarget();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Realtime Texture Baker")
	bool BakeUVTexture();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Realtime Texture Baker")
	bool BakeCameraProjection();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Realtime Texture Baker")
	bool BakeCurrentMode();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Realtime Texture Baker")
	void ClearOutput();
};
