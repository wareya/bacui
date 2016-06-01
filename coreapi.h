#include "include/m64p_config.h"
#include "include/m64p_common.h"
#include "include/m64p_frontend.h"
#include "include/m64p_debugger.h"

#define COREAPI \
XM(CoreGetAPIVersions)\
XM(CoreStartup)\
XM(CoreAttachPlugin)\
XM(CoreDetachPlugin)\
XM(CoreDoCommand)\
XM(CoreErrorMessage)\
\
XM(ConfigSaveFile)\
\
XM(DebugSetCallbacks)\
XM(DebugSetRunState)\
XM(DebugGetState)\
XM(DebugStep)\
XM(DebugMemGetPointer)\
XM(DebugMemRead32)
