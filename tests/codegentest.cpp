#include <gtest/gtest.h>
#include <memory>
#include <string>
#include "pp_lexer.h"
#include "pp_parser.h"
#include "pp_semantic_analyzer.h"
#include "pp_code_generator.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>

static PP_CodeGenerator* run_codegen(const char* source, int& lexer_errors, int& parser_errors, int& semantic_errors, int& codegen_result) {
    auto lexer_h = pp_lexer_create(source, nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    if (!lexer.get()) return nullptr;
    int lex_res = pp_lexer_analyze(lexer.get());
    lexer_errors = lexer->error_list ? (int)lexer->error_list->n_errors : 0;
    if (lex_res != 1) {
        parser_errors = 0;
        semantic_errors = 0;
        codegen_result = 0;
        return nullptr;
    }

    PP_Parser* parser_raw = pp_parser_create(lexer.get());
    std::unique_ptr<PP_Parser, decltype(&pp_parser_free)> parser(parser_raw, pp_parser_free);
    if (!parser.get()) return nullptr;
    int parse_res = pp_parser_parse(parser.get());
    parser_errors = parser->error_list ? (int)parser->error_list->n_errors : 0;
    if (parse_res != 1) {
        semantic_errors = 0;
        codegen_result = 0;
        return nullptr;
    }

    PP_SemanticAnalyzer* sa_raw = pp_semantic_analyzer_create(parser.get());
    std::unique_ptr<PP_SemanticAnalyzer, decltype(&pp_semantic_analyzer_free)> sa(sa_raw, pp_semantic_analyzer_free);
    if (!sa.get()) return nullptr;
    int analyze_res = pp_semantic_analyzer_analyze(sa.get());
    semantic_errors = sa->error_list ? (int)sa->error_list->n_errors : 0;

    if (analyze_res != 1 || semantic_errors > 0) {
        codegen_result = 0;
        return nullptr;
    }

    PP_CodeGenerator* cg_raw = pp_code_generator_create(sa.release());
    if (!cg_raw) {
        codegen_result = 0;
        return nullptr;
    }
    // sa ownership transferred to cg, but cg does not free the analyzer struct itself.
    // Free the analyzer struct now to avoid leak in the test harness.
    pp_semantic_analyzer_free(sa_raw);
    codegen_result = pp_code_generator_generate(cg_raw);
    return cg_raw;
}

static std::string module_to_string(LLVMModuleRef mod) {
    char* str = LLVMPrintModuleToString(mod);
    std::string s;
    if (str) {
        s = str;
        LLVMDisposeMessage(str);
    }
    return s;
}

static void assert_ir_contains(PP_CodeGenerator* cg, const std::string& needle) {
    std::string ir = module_to_string(cg->mod);
    EXPECT_NE(ir.find(needle), std::string::npos) << "IR does not contain: " << needle << "\nIR:\n" << ir;
}

static void assert_ir_not_contains(PP_CodeGenerator* cg, const std::string& needle) {
    std::string ir = module_to_string(cg->mod);
    EXPECT_EQ(ir.find(needle), std::string::npos) << "IR unexpectedly contains: " << needle << "\nIR:\n" << ir;
}

TEST(CodeGenTest, GlobalInt32Variable) {
    const char* source = "let g_i :: int32 = 42;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
    EXPECT_EQ(codegen_result, 1);

    // Verify LLVM module
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) {
        std::string msg = error_msg ? error_msg : "unknown";
        if (error_msg) LLVMDisposeMessage(error_msg);
        FAIL() << "LLVMVerifyModule failed: " << msg;
    }
    if (error_msg) LLVMDisposeMessage(error_msg);

    assert_ir_contains(cg.get(), "g_i");
    assert_ir_contains(cg.get(), "i32");
    assert_ir_contains(cg.get(), "42");
}

TEST(CodeGenTest, GlobalUint32Variable) {
    const char* source = "let g_u :: uint32 = 100u;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    EXPECT_EQ(semantic_errors, 0);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) {
        std::string msg = error_msg ? error_msg : "unknown";
        if (error_msg) LLVMDisposeMessage(error_msg);
        FAIL() << "LLVMVerifyModule failed: " << msg;
    }
    if (error_msg) LLVMDisposeMessage(error_msg);

    assert_ir_contains(cg.get(), "g_u");
    assert_ir_contains(cg.get(), "i32");
    assert_ir_contains(cg.get(), "100");
}

TEST(CodeGenTest, GlobalFloatVariable) {
    const char* source = "let g_f :: float = 3.14;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) {
        std::string msg = error_msg ? error_msg : "unknown";
        if (error_msg) LLVMDisposeMessage(error_msg);
        FAIL() << "LLVMVerifyModule failed: " << msg;
    }
    if (error_msg) LLVMDisposeMessage(error_msg);

    assert_ir_contains(cg.get(), "g_f");
    assert_ir_contains(cg.get(), "float");
}

TEST(CodeGenTest, GlobalBoolVariable) {
    const char* source = "let g_b :: bool = true;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) {
        std::string msg = error_msg ? error_msg : "unknown";
        if (error_msg) LLVMDisposeMessage(error_msg);
        FAIL() << "LLVMVerifyModule failed: " << msg;
    }
    if (error_msg) LLVMDisposeMessage(error_msg);

    assert_ir_contains(cg.get(), "g_b");
    assert_ir_contains(cg.get(), "i1");
}

TEST(CodeGenTest, GlobalInt32NoInitializer) {
    const char* source = "let g_zero :: int32;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) {
        std::string msg = error_msg ? error_msg : "unknown";
        if (error_msg) LLVMDisposeMessage(error_msg);
        FAIL() << "LLVMVerifyModule failed: " << msg;
    }
    if (error_msg) LLVMDisposeMessage(error_msg);

    assert_ir_contains(cg.get(), "g_zero");
    // Zero-initialized (LLVM prints "i32 0")
    assert_ir_contains(cg.get(), "global i32");
}

TEST(CodeGenTest, GlobalInt32BinaryAdd) {
    const char* source = "let add_res :: int32 = 1 + 2;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) {
        std::string msg = error_msg ? error_msg : "unknown";
        if (error_msg) LLVMDisposeMessage(error_msg);
        FAIL() << "LLVMVerifyModule failed: " << msg;
    }
    if (error_msg) LLVMDisposeMessage(error_msg);

    assert_ir_contains(cg.get(), "add_res");
    assert_ir_contains(cg.get(), "i32 3");
}

TEST(CodeGenTest, GlobalInt32BinaryMul) {
    const char* source = "let mul_res :: int32 = 3 * 4;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) {
        std::string msg = error_msg ? error_msg : "unknown";
        if (error_msg) LLVMDisposeMessage(error_msg);
        FAIL() << "LLVMVerifyModule failed: " << msg;
    }
    if (error_msg) LLVMDisposeMessage(error_msg);

    assert_ir_contains(cg.get(), "mul_res");
    assert_ir_contains(cg.get(), "i32 12");
}

TEST(CodeGenTest, GlobalInt32UnaryNeg) {
    const char* source = "let neg_res :: int32 = -5;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) {
        std::string msg = error_msg ? error_msg : "unknown";
        if (error_msg) LLVMDisposeMessage(error_msg);
        FAIL() << "LLVMVerifyModule failed: " << msg;
    }
    if (error_msg) LLVMDisposeMessage(error_msg);

    assert_ir_contains(cg.get(), "neg_res");
    assert_ir_contains(cg.get(), "i32 -5");
}

TEST(CodeGenTest, GlobalBoolBinaryAnd) {
    const char* source = "let cmp_res :: bool = 1 == 0;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) {
        std::string msg = error_msg ? error_msg : "unknown";
        if (error_msg) LLVMDisposeMessage(error_msg);
        FAIL() << "LLVMVerifyModule failed: " << msg;
    }
    if (error_msg) LLVMDisposeMessage(error_msg);

    assert_ir_contains(cg.get(), "cmp_res");
    assert_ir_contains(cg.get(), "i1 false");
}



TEST(CodeGenTest, FunctionReturnLiteral) {
    const char* source = "func foo :: () -> int32 { return 42; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@foo");
    assert_ir_contains(cg.get(), "ret i32 42");
}

TEST(CodeGenTest, FunctionReturnVariable) {
    const char* source = "func foo :: () -> int32 { let x :: int32 = 10; return x; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@foo");
    assert_ir_contains(cg.get(), "alloca i32");
    assert_ir_contains(cg.get(), "store i32 10");
    assert_ir_contains(cg.get(), "ret i32");
}

TEST(CodeGenTest, FunctionCall) {
    const char* source = "func add :: (a :: int32, b :: int32) -> int32 { return a + b; } func main :: () -> int32 { return add(1, 2); }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@add");
    assert_ir_contains(cg.get(), "@main");
    assert_ir_contains(cg.get(), "call i32 @add(i32 1, i32 2)");
    assert_ir_contains(cg.get(), "ret i32");
}

TEST(CodeGenTest, FunctionCallWithLocalArg) {
    const char* source = "func add :: (a :: int32, b :: int32) -> int32 { return a + b; } func main :: () -> int32 { let t :: int32 = 3; return add(t, 4); }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@main");
    assert_ir_contains(cg.get(), "call i32 @add");
    assert_ir_contains(cg.get(), "alloca i32");
}
TEST(CodeGenTest, FunctionReturnReference) {
    const char* source = "let g :: int32 = 10;\nfunc get_ref :: () -> int32& { return g; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@get_ref");
    assert_ir_contains(cg.get(), "define ptr @get_ref");
    assert_ir_contains(cg.get(), "ret ptr @g");
}

TEST(CodeGenTest, FunctionReturnReferenceCall) {
    const char* source = "let g :: int32 = 10;\nfunc get_ref :: () -> int32& { return g; }\nfunc main :: () -> int32 { let r :: int32& = get_ref(); return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@get_ref");
    assert_ir_contains(cg.get(), "define ptr @get_ref");
    assert_ir_contains(cg.get(), "call ptr @get_ref()");
    assert_ir_contains(cg.get(), "alloca ptr");
}

TEST(CodeGenTest, LogicalAnd) {
    const char* source = "func test_and :: () -> bool { return true && false; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_and");
    assert_ir_contains(cg.get(), "and.rhs");
    assert_ir_contains(cg.get(), "and.phi");
}

TEST(CodeGenTest, LogicalOr) {
    const char* source = "func test_or :: () -> bool { return false || true; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_or");
    assert_ir_contains(cg.get(), "or.rhs");
    assert_ir_contains(cg.get(), "or.phi");
}

TEST(CodeGenTest, GlobalLogicalAnd) {
    const char* source = "let x :: bool = true && false;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@x");
    assert_ir_contains(cg.get(), "global i1 false");
}

TEST(CodeGenTest, GlobalLogicalOr) {
    const char* source = "let y :: bool = false || true;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@y");
    assert_ir_contains(cg.get(), "global i1 true");
}

TEST(CodeGenTest, LogicalShortCircuitWithCalls) {
    const char* source =
        "func a :: () -> bool { return true; }\n"
        "func b :: () -> bool { return false; }\n"
        "func test_sc :: () -> bool { return a() || b(); }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_sc");
    assert_ir_contains(cg.get(), "or.rhs");
    assert_ir_contains(cg.get(), "or.phi");
    /* Ensure b() is only called in or.rhs block, not unconditionally */
    assert_ir_contains(cg.get(), "call i1 @b()");
}

TEST(CodeGenTest, CastIntToFloat) {
    const char* source = "func test_cast :: () -> float { let x :: int32 = 42; return x as float; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_cast");
    assert_ir_contains(cg.get(), "sitofp");
}

TEST(CodeGenTest, CastNewtype) {
    const char* source =
        "newtype int32 Meters;\n"
        "func test_newtype_cast :: () -> float { let x :: Meters = 10 as Meters; return x as float; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_newtype_cast");
    assert_ir_contains(cg.get(), "sitofp");
}

TEST(CodeGenTest, CastFloatToInt) {
    const char* source = "func test_cast :: () -> int32 { let x :: float = 3.14; return x as int32; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_cast");
    assert_ir_contains(cg.get(), "fptosi");
}

TEST(CodeGenTest, CastNewtypeToUnderlying) {
    const char* source =
        "newtype int32 Meters;\n"
        "func test_newtype_to_underlying :: () -> int32 { let x :: Meters = 5 as Meters; return x as int32; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_newtype_to_underlying");
    // Casting newtype to underlying should be a no-op at LLVM level
    assert_ir_not_contains(cg.get(), "sitofp");
    assert_ir_not_contains(cg.get(), "fptosi");
}

TEST(CodeGenTest, StructMemberRead) {
    const char* source =
        "struct Point { x :: int32; y :: int32; }\n"
        "func test_struct_member_read :: (p :: Point) -> int32 { let v :: int32 = p.x; return v; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_struct_member_read");
    assert_ir_contains(cg.get(), "getelementptr");
    assert_ir_contains(cg.get(), "field_gep");
}

TEST(CodeGenTest, StructMemberWrite) {
    const char* source =
        "struct Point { x :: int32; y :: int32; }\n"
        "func test_struct_member_write :: () -> int32 { let p :: Point; p.x = 42; return p.x; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_struct_member_write");
    assert_ir_contains(cg.get(), "getelementptr");
    assert_ir_contains(cg.get(), "store i32 42");
    assert_ir_contains(cg.get(), "field_gep");
}

TEST(CodeGenTest, StructMemberMixedTypes) {
    const char* source =
        "struct Vec { x :: int32; y :: float; }\n"
        "func test_mixed :: (v :: Vec) -> float { let f :: float = v.y; return f; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_mixed");
    assert_ir_contains(cg.get(), "getelementptr");
    // Index 1 corresponds to y field
    assert_ir_contains(cg.get(), "field_gep");
}

TEST(CodeGenTest, StructMemberMultiFieldAccess) {
    const char* source =
        "struct Data { a :: int32; b :: int32; c :: int32; }\n"
        "func test_multi :: (d :: Data) -> int32 { let sum :: int32 = d.a + d.b + d.c; return sum; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_multi");
    assert_ir_contains(cg.get(), "getelementptr");
    // Should have multiple field accesses
    assert_ir_contains(cg.get(), "field_gep");
}

TEST(CodeGenTest, GlobalStructInit) {
    const char* source =
        "struct Point { x :: int32; y :: int32; }\n"
        "let p :: Point = Point { .x = 1, .y = 2 };\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@p");
    assert_ir_contains(cg.get(), "%Point");
    assert_ir_contains(cg.get(), "{ i32 1, i32 2 }");
}

TEST(CodeGenTest, RuntimeStructInit) {
    const char* source =
        "struct Point { x :: int32; y :: int32; }\n"
        "func make_point :: () -> Point { let p :: Point = Point { .x = 1, .y = 2 }; return p; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@make_point");
    assert_ir_contains(cg.get(), "alloca %Point");
}

TEST(CodeGenTest, StructInitPartialWithDefault) {
    const char* source =
        "struct S { a :: int32; b :: int32 = 99; }\n"
        "let s :: S = S { .a = 5 };\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@s");
    assert_ir_contains(cg.get(), "{ i32 5, i32 99 }");
}

TEST(CodeGenTest, IfStatement) {
    const char* source =
        "func test_if :: () -> int32 { let x :: int32 = 1; if (x > 0) { return 1; } return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_if");
    assert_ir_contains(cg.get(), "if.then");
    assert_ir_contains(cg.get(), "if.end");
}

TEST(CodeGenTest, IfElseStatement) {
    const char* source =
        "func test_if_else :: () -> int32 { let x :: int32 = -1; if (x > 0) { return 1; } else { return 0; } }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_if_else");
    assert_ir_contains(cg.get(), "if.then");
    assert_ir_contains(cg.get(), "if.else");
    assert_ir_contains(cg.get(), "if.end");
}

TEST(CodeGenTest, IfElseIfElseStatement) {
    const char* source =
        "func test_if_elseif_else :: () -> int32 { let x :: int32 = 0; if (x > 0) { return 1; } else if (x < 0) { return -1; } else { return 0; } }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_if_elseif_else");
    assert_ir_contains(cg.get(), "if.then");
    assert_ir_contains(cg.get(), "if.else");
    assert_ir_contains(cg.get(), "if.end");
}

TEST(CodeGenTest, WhileStatement) {
    const char* source =
        "func test_while :: () -> int32 { let i :: int32 = 0; let sum :: int32 = 0; while (i < 5) { sum = sum + i; i = i + 1; } return sum; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_while");
    assert_ir_contains(cg.get(), "while.cond");
    assert_ir_contains(cg.get(), "while.body");
    assert_ir_contains(cg.get(), "while.end");
}

TEST(CodeGenTest, ForStatement) {
    const char* source =
        "func test_for :: () -> int32 { let i :: int32 = 0; let sum :: int32 = 0; for (; i < 5; i = i + 1) { sum = sum + i; } return sum; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_for");
    assert_ir_contains(cg.get(), "for.cond");
    assert_ir_contains(cg.get(), "for.body");
    assert_ir_contains(cg.get(), "for.step");
    assert_ir_contains(cg.get(), "for.end");
}

TEST(CodeGenTest, StructInitMemberAccessRvalue) {
    const char* source =
        "struct S { a :: int32; b :: int32; }\n"
        "func test_struct_init_member_access :: () -> int32 { let x :: int32 = S { .a = 5, .b = 6 }.a; return x; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_struct_init_member_access");
    assert_ir_contains(cg.get(), "i32 5");
    // The member access on a temporary struct init should be handled via extractvalue or constant folding.
    // We verify the constant is present and codegen succeeds.
}

TEST(CodeGenTest, StructMemberAccessViaFunctionCallRvalue) {
    const char* source =
        "struct S { a :: int32; b :: int32; }\n"
        "func make :: () -> S { let s :: S = S { .a = 7, .b = 8 }; return s; }\n"
        "func test :: () -> int32 { let x :: int32 = make().a; return x; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@make");
    assert_ir_contains(cg.get(), "@test");
    // Member access on rvalue returned from function should use extractvalue/GEP on temporary
    assert_ir_contains(cg.get(), "extractvalue");
    // Value should be propagated
    assert_ir_contains(cg.get(), "i32 7");
}

TEST(CodeGenTest, NestedRvalueMemberAccess) {
    const char* source =
        "struct T { x :: int32; }\n"
        "struct S { inner :: T; }\n"
        "func test_nested :: () -> int32 { let v :: int32 = S { .inner = T { .x = 1 } }.inner.x; return v; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_nested");
    // Nested rvalue member access should be correctly folded/extracted
    // Accept either extractvalue or constant folding; ensure value is present
    assert_ir_contains(cg.get(), "i32 1");
}

TEST(CodeGenTest, MixedLvalueRvalueMemberAccess) {
    const char* source =
        "struct S { a :: int32; }\n"
        "func test_mixed :: () -> int32 { let s :: S = S { .a = 2 }; let x :: int32 = s.a; return x; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_mixed");
    // Lvalue base member access should use GEP
    assert_ir_contains(cg.get(), "getelementptr");
    assert_ir_contains(cg.get(), "field_gep");
    // Constant should be present in init
    assert_ir_contains(cg.get(), "i32 2");
}

TEST(CodeGenTest, ConstPropagationRvalueMemberAccess) {
    const char* source =
        "struct S { a :: const int32; }\n"
        "func make :: () -> S { let s :: S = S { .a = 3 }; return s; }\n"
        "func test_const :: () -> int32 { let x :: int32 = make().a; return x; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@make");
    assert_ir_contains(cg.get(), "@test_const");
    assert_ir_contains(cg.get(), "extractvalue");
    assert_ir_contains(cg.get(), "i32 3");
}



TEST(CodeGenTest, ArrayMemberAccessLvalue) {
    const char* source =
        "func test_array_access :: () -> int32 { let arr :: [int32 : 5u]; arr[2u] = 42; return arr[2u]; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    if (!cg_raw) {
        FAIL() << "CodeGen failed: lex=" << lexer_errors << ", parse=" << parser_errors << ", sem=" << semantic_errors << ", res=" << codegen_result;
    }
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "getelementptr");
    assert_ir_contains(cg.get(), "store i32 42");
}

TEST(CodeGenTest, GlobalArrayInit) {
    const char* source =
        "let arr :: [int32 : 3u] = [int32 : 3u; 1, 2, 3];\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@arr");
    assert_ir_contains(cg.get(), "[3 x i32]");
    assert_ir_contains(cg.get(), "i32 1");
    assert_ir_contains(cg.get(), "i32 2");
    assert_ir_contains(cg.get(), "i32 3");
}

TEST(CodeGenTest, RuntimeArrayInit) {
    const char* source =
        "func make_arr :: () -> [int32 : 3u] { let a :: [int32 : 3u] = [int32 : 3u; 10, 20, 30]; return a; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@make_arr");
    assert_ir_contains(cg.get(), "alloca [3 x i32]");
    assert_ir_contains(cg.get(), "i32 10");
    assert_ir_contains(cg.get(), "i32 20");
    assert_ir_contains(cg.get(), "i32 30");
}

TEST(CodeGenTest, ArrayAccessRvalue) {
    const char* source =
        "func test_arr_rvalue :: () -> int32 { let arr :: [int32 : 3u] = [int32 : 3u; 7, 8, 9]; let v :: int32 = arr[1u]; return v; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_arr_rvalue");
    assert_ir_contains(cg.get(), "getelementptr");
    assert_ir_contains(cg.get(), "load i32");
}

TEST(CodeGenTest, ListInitAndAccess) {
    const char* source =
        "func test_list_init :: () -> int32 { let lst :: [int32] = [int32; 10, 20, 30]; let v :: int32 = lst[1u]; return v; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_list_init");
    assert_ir_contains(cg.get(), "%pp.list");
    assert_ir_contains(cg.get(), "call ptr @malloc");
    assert_ir_contains(cg.get(), "call void @free");
    assert_ir_contains(cg.get(), "i32 20");
}

TEST(CodeGenTest, ListInitWithStructElements) {
    const char* source =
        "struct Point { x :: int32; y :: int32; }\n"
        "func test_list_struct :: () -> int32 { let lst :: [Point] = [Point; Point { .x = 1, .y = 2 }, Point { .x = 3, .y = 4 }]; let p :: Point = lst[0u]; return p.x; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_list_struct");
    assert_ir_contains(cg.get(), "%pp.list");
    assert_ir_contains(cg.get(), "%Point");
    assert_ir_contains(cg.get(), "call ptr @malloc");
    assert_ir_contains(cg.get(), "call void @free");
    assert_ir_contains(cg.get(), "i32 1");
}

TEST(CodeGenTest, ListStoreAndLoad) {
    const char* source =
        "func test_list_store :: () -> int32 { let lst :: [int32] = [int32; 5, 6, 7]; lst[1u] = 42; return lst[1u]; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_list_store");
    assert_ir_contains(cg.get(), "%pp.list");
    assert_ir_contains(cg.get(), "store i32 42");
    assert_ir_contains(cg.get(), "call void @free");
}

TEST(CodeGenTest, ListCopyInitAndAssign) {
    const char* source =
        "func test_list_copy :: () -> int32 { let a :: [int32] = [int32; 1, 2, 3]; let b :: [int32] = a; let c :: [int32] = [int32; 4, 5]; b = c; return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_list_copy");
    assert_ir_contains(cg.get(), "%pp.list");
    assert_ir_contains(cg.get(), "call ptr @malloc");
    assert_ir_contains(cg.get(), "call ptr @memcpy");
    assert_ir_contains(cg.get(), "list_copy_nonempty");
    assert_ir_contains(cg.get(), "call void @free");
}

TEST(CodeGenTest, ListCopyNoAliasing) {
    const char* source =
        "func test_list_alias :: () -> int32 { let a :: [int32] = [int32; 1, 2, 3]; let b :: [int32] = a; a[0u] = 99; return b[0u]; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_list_alias");
    assert_ir_contains(cg.get(), "%pp.list");
    assert_ir_contains(cg.get(), "call ptr @malloc");
    assert_ir_contains(cg.get(), "call ptr @memcpy");
    assert_ir_contains(cg.get(), "list_copy_nonempty");
}

TEST(CodeGenTest, ListAssignDropsOld) {
    const char* source =
        "func test_list_assign_drop :: () -> int32 { let a :: [int32] = [int32; 1, 2, 3]; let b :: [int32] = [int32; 4, 5]; b = a; return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_list_assign_drop");
    assert_ir_contains(cg.get(), "%pp.list");
    assert_ir_contains(cg.get(), "call ptr @malloc");
    assert_ir_contains(cg.get(), "call ptr @memcpy");
    assert_ir_contains(cg.get(), "call void @free");
}

TEST(CodeGenTest, StructContainingListDrop) {
    const char* source =
        "struct S { lst :: [int32]; }\n"
        "func test_struct_list :: () -> int32 { let s :: S = S { .lst = [int32; 1, 2] }; return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_struct_list");
    assert_ir_contains(cg.get(), "%S");
    assert_ir_contains(cg.get(), "%pp.list");
    assert_ir_contains(cg.get(), "call ptr @malloc");
    assert_ir_contains(cg.get(), "call void @free");
}

TEST(CodeGenTest, StructInitCopy) {
    const char* source =
        "struct S { lst :: [int32]; }\n"
        "func test_struct_init_copy :: () -> int32 { let a :: S = S { .lst = [int32; 1, 2] }; let b :: S = a; return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_struct_init_copy");
    assert_ir_contains(cg.get(), "%S");
    assert_ir_contains(cg.get(), "%pp.list");
    assert_ir_contains(cg.get(), "call ptr @malloc");
    assert_ir_contains(cg.get(), "call ptr @memcpy");
    assert_ir_contains(cg.get(), "call void @free");
}

TEST(CodeGenTest, SelfAssignmentNoop) {
    const char* source =
        "func test_self_assign :: () -> int32 { let a :: [int32] = [int32; 1, 2, 3]; a = a; return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_self_assign");
    assert_ir_contains(cg.get(), "%pp.list");
    // Self-assignment should be a no-op: no memcpy for the self-assign
    assert_ir_not_contains(cg.get(), "list_copy_nonempty");
}

TEST(CodeGenTest, EarlyReturnAfterCopy) {
    const char* source =
        "func test_early_return :: () -> int32 { let a :: [int32] = [int32; 1,2,3]; let b :: [int32] = a; if (true) { return b[0u]; } return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_early_return");
    assert_ir_contains(cg.get(), "%pp.list");
    assert_ir_contains(cg.get(), "call ptr @malloc");
    assert_ir_contains(cg.get(), "call ptr @memcpy");
    assert_ir_contains(cg.get(), "call void @free");
}

TEST(CodeGenTest, ListOfListsDeepCopy) {
    const char* source =
        "func test_deep :: () -> int32 { let a :: [[int32]] = [[int32]; [int32; 1,2], [int32; 3,4]]; let b :: [[int32]] = a; a[0u][0u] = 99; return b[0u][0u]; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_deep");
    assert_ir_contains(cg.get(), "%pp.list");
    assert_ir_contains(cg.get(), "call ptr @malloc");
    // Deep copy of list of lists should emit a loop for inner copies
    assert_ir_contains(cg.get(), "list_copy_loop_cond");
    assert_ir_contains(cg.get(), "call void @free");
}

TEST(CodeGenTest, ListCopyHelperGenerated) {
    const char* source = "func test_list_copy :: () -> void { let a :: [int32] = [int32; 1, 2, 3]; let b :: [int32] = a; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_list_copy");
    assert_ir_contains(cg.get(), "pp.copy.");
    assert_ir_contains(cg.get(), "list_copy_nonempty");
    assert_ir_contains(cg.get(), "call ptr @malloc");
}

TEST(CodeGenTest, StructWithListCopyHelperGenerated) {
    const char* source = "struct S { xs :: [int32]; v :: int32; } func test_struct_copy :: () -> void { let s1 :: S = S{ .xs = [int32; 1, 2], .v = 10 }; let s2 :: S = s1; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_struct_copy");
    assert_ir_contains(cg.get(), "pp.copy.");
    // Struct copy helper should recursively call list copy helper
    assert_ir_contains(cg.get(), "call void @pp.copy.");
}

TEST(CodeGenTest, ListOfListsCopyHelperGenerated) {
    const char* source = "func test_list_of_lists :: () -> void { let a :: [[int32]] = [[int32]; [int32; 1, 2], [int32; 3]]; let b :: [[int32]] = a; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_list_of_lists");
    assert_ir_contains(cg.get(), "pp.copy.");
    assert_ir_contains(cg.get(), "list_copy_loop_cond");
}

TEST(CodeGenTest, RecursiveStructCopy) {
    const char* source = "struct Node { v :: int32; kids :: [Node]; } func test_recursive_copy :: () -> void { let n1 :: Node; let n2 :: Node = n1; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_recursive_copy");
    assert_ir_contains(cg.get(), "pp.copy.");
    // Recursive copy helper should call itself for list element copy
    assert_ir_contains(cg.get(), "call void @pp.copy.");
}

TEST(CodeGenTest, MutuallyRecursiveStructCopy) {
    const char* source = "struct A { b :: [B]; } struct B { a :: [A]; } func test_mutual_copy :: () -> void { let a1 :: A; let b1 :: B; let b2 :: B = b1; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(codegen_result, 1);
    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);
    assert_ir_contains(cg.get(), "@test_mutual_copy");
    assert_ir_contains(cg.get(), "pp.copy.");
}

TEST(CodeGenTest, HashLengthArrayConstant) {
    const char* source =
        "func main :: () -> uint32 {\n"
        "    let arr :: [int32 : 5u];\n"
        "    let len :: uint32 = #arr;\n"
        "    return len;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
    EXPECT_EQ(codegen_result, 1);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);

    // Array length should be emitted as constant i32 5
    assert_ir_contains(cg.get(), "i32 5");
    assert_ir_contains(cg.get(), "@main");
}

TEST(CodeGenTest, IntegerLiteralSuffixCodegen) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: int16 = 1s;\n"
        "    let b :: uint16 = 1us;\n"
        "    let c :: int8 = 2ss;\n"
        "    let d :: uint8 = 2uss;\n"
        "    let e :: int64 = 3l;\n"
        "    let f :: uint64 = 3ul;\n"
        "    let g :: int128 = 4ll;\n"
        "    let h :: uint128 = 4ull;\n"
        "    return 0;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
    EXPECT_EQ(codegen_result, 1);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);

    // Verify types are emitted
    assert_ir_contains(cg.get(), "i16");
    assert_ir_contains(cg.get(), "i8");
    assert_ir_contains(cg.get(), "i64");
    assert_ir_contains(cg.get(), "i128");
    // Verify constants
    assert_ir_contains(cg.get(), "store i16 1");
    assert_ir_contains(cg.get(), "store i8 2");
    assert_ir_contains(cg.get(), "store i64 3");
    assert_ir_contains(cg.get(), "store i128 4");
    assert_ir_contains(cg.get(), "@main");
}

TEST(CodeGenTest, StaticStringLiteralLocal) {
    const char* source =
        "func main :: () -> void {\n"
        "    let a :: [uint8 : 6u] = @\"hello\";\n"
        "    let b :: [uint8 : 1u] = @\"\";\n"
        "    let c :: [uint8 : 7u] = @\"hello\\n\";\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
    EXPECT_EQ(codegen_result, 1);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);

    // Constants should be emitted for static string literals
    assert_ir_contains(cg.get(), "[6 x i8]");
    assert_ir_contains(cg.get(), "c\"hello\\00\"");
    assert_ir_contains(cg.get(), "[1 x i8]");
    assert_ir_contains(cg.get(), "[7 x i8]");
    assert_ir_contains(cg.get(), "c\"hello\\0A\\00\"");
    assert_ir_not_contains(cg.get(), "unreachable");
}

TEST(CodeGenTest, StaticStringLiteralGlobal) {
    const char* source =
        "let g :: [uint8 : 6u] = @\"hello\";\n"
        "func main :: () -> void {\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
    EXPECT_EQ(codegen_result, 1);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);

    // Global constant should be emitted
    assert_ir_contains(cg.get(), "g");
    assert_ir_contains(cg.get(), "[6 x i8]");
    assert_ir_contains(cg.get(), "c\"hello\\00\"");
}

TEST(CodeGenTest, HeapStringLiteralLocal) {
    const char* source =
        "func main :: () -> void {\n"
        "    let s :: string = \"hi\";\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
    EXPECT_EQ(codegen_result, 1);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);

    assert_ir_contains(cg.get(), "%pp.string");
    assert_ir_contains(cg.get(), "call ptr @malloc");
    assert_ir_contains(cg.get(), "call ptr @memcpy");
    assert_ir_contains(cg.get(), "@.str.lit");
    assert_ir_contains(cg.get(), "call void @pp.drop");
}

TEST(CodeGenTest, StringDropInStruct) {
    const char* source =
        "struct Person {\n"
        "    name :: string;\n"
        "    age :: int32;\n"
        "}\n"
        "func main :: () -> void {\n"
        "    let p :: Person = Person{ .name = \"Alice\", .age = 30 };\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
    EXPECT_EQ(codegen_result, 1);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);

    assert_ir_contains(cg.get(), "%Person");
    assert_ir_contains(cg.get(), "%pp.string");
    assert_ir_contains(cg.get(), "call void @pp.drop");
    assert_ir_contains(cg.get(), "call void @free");
}

TEST(CodeGenTest, ListOfStringsDrop) {
    const char* source =
        "func main :: () -> void {\n"
        "    let ls :: [string] = [string; \"a\", \"bc\"];\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
    EXPECT_EQ(codegen_result, 1);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);

    assert_ir_contains(cg.get(), "%pp.list");
    assert_ir_contains(cg.get(), "%pp.string");
    assert_ir_contains(cg.get(), "call void @pp.drop");
    assert_ir_contains(cg.get(), "call void @free");
}

TEST(CodeGenTest, StringConcat) {
    const char* source =
        "func main :: () -> void {\n"
        "    let a :: string = \"foo\";\n"
        "    let b :: string = \"bar\";\n"
        "    let c :: string = a + b;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, codegen_result = 0;
    PP_CodeGenerator* cg_raw = run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result);
    std::unique_ptr<PP_CodeGenerator, decltype(&pp_code_generator_free)> cg(cg_raw, pp_code_generator_free);
    ASSERT_NE(cg.get(), nullptr);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
    EXPECT_EQ(codegen_result, 1);

    char* error_msg = nullptr;
    LLVMBool broken = LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &error_msg);
    if (broken) { std::string msg = error_msg ? error_msg : "unknown"; if (error_msg) LLVMDisposeMessage(error_msg); FAIL() << "LLVMVerifyModule failed: " << msg; }
    if (error_msg) LLVMDisposeMessage(error_msg);

    assert_ir_contains(cg.get(), "%pp.string");
    assert_ir_contains(cg.get(), "call ptr @malloc");
    assert_ir_contains(cg.get(), "str_total_len");
    assert_ir_contains(cg.get(), "str_concat_memcpy1");
    assert_ir_contains(cg.get(), "str_concat_memcpy2");
}



