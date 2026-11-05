#ifndef PP_CODE_OPTIMIZATION_H
#define PP_CODE_OPTIMIZATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pp_code_generator.h"

int pp_optimize_module(LLVMModuleRef mod, LLVMTargetMachineRef target_machine, char opt_char);

#ifdef __cplusplus
}
#endif

#endif /* PP_CODE_OPTIMIZATION_H */