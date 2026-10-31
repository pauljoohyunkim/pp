#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <llvm-c/Transforms/PassBuilder.h>

#include "pp_code_optimization.h"

#define NULL_POINTER_ERROR_HANDLE(ptr)         \
    do {                                       \
        if ((ptr) == NULL) goto error_cleanup; \
    } while (0)

int pp_optimize_module(LLVMModuleRef mod, LLVMTargetMachineRef target_machine, char opt_char) {
    assert(mod);
    assert(target_machine);

    const char* pipeline_str = NULL;
    switch (opt_char) {
        case '0':
            pipeline_str = "default<O0>";
            break;
        case '1':
            pipeline_str = "default<O1>";
            break;
        case '2':
            pipeline_str = "default<O2>";
            break;
        case '3':
            pipeline_str = "default<O3>";
            break;
        case 's':
            pipeline_str = "default<Os>";
            break;
        case 'z':
            pipeline_str = "default<Oz>";
            break;
        default:
            abort();
    }

    LLVMPassBuilderOptionsRef options = LLVMCreatePassBuilderOptions();
    NULL_POINTER_ERROR_HANDLE(options);

    LLVMErrorRef error = LLVMRunPasses(mod, pipeline_str, target_machine, options);
    if (error) {
        char* err_msg = LLVMGetErrorMessage(error);
        fprintf(stderr, "Optimization failure");
        LLVMDisposeErrorMessage(err_msg);
    }

    LLVMDisposePassBuilderOptions(options);
    options = NULL;

    return 1;

error_cleanup:
    if (options) LLVMDisposePassBuilderOptions(options);
    return 0;
}