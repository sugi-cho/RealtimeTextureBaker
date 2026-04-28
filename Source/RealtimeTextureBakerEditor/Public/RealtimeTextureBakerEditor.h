#pragma once

#include "Modules/ModuleManager.h"

class FRealtimeTextureBakerEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
