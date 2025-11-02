#include "SkaldDiceModule.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogSkaldDice);

class FSkaldDiceModule : public IModuleInterface
{
public:
    virtual void StartupModule() override {}
    virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FSkaldDiceModule, SkaldDice);
