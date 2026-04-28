#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RealtimeTextureBakerEditorLibrary.generated.h"

class UTexture2D;
class UTextureRenderTarget2D;

UCLASS()
class REALTIMETEXTUREBAKEREDITOR_API URealtimeTextureBakerEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Realtime Texture Baker|Editor")
	static UTexture2D* SaveRenderTargetToTextureAsset(UTextureRenderTarget2D* RenderTarget, const FString& PackagePath, const FString& AssetName, bool bSavePackage);
};
