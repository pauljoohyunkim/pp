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
    // sa ownership transferred to cg, so we must not free sa
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

