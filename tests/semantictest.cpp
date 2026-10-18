#include <gtest/gtest.h>
#include <memory>
#include "pp_lexer.h"
#include "pp_parser.h"
#include "pp_semantic_analyzer.h"

static void semantic_analyze_source(const char* source, int& lexer_errors, int& parser_errors, int& semantic_errors, int& analyze_result) {
    auto lexer_h = pp_lexer_create(source, nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_NE(lexer.get(), nullptr);
    int lex_res = pp_lexer_analyze(lexer.get());
    lexer_errors = lexer->error_list ? (int)lexer->error_list->n_errors : 0;
    if (lex_res != 1) {
        parser_errors = 0;
        semantic_errors = 0;
        analyze_result = 0;
        return;
    }
    PP_Parser* parser_raw = pp_parser_create(lexer.get());
    std::unique_ptr<PP_Parser, decltype(&pp_parser_free)> parser(parser_raw, pp_parser_free);
    ASSERT_NE(parser.get(), nullptr);
    int parse_res = pp_parser_parse(parser.get());
    parser_errors = parser->error_list ? (int)parser->error_list->n_errors : 0;
    if (parse_res != 1) {
        semantic_errors = 0;
        analyze_result = 0;
        return;
    }
    PP_SemanticAnalyzer* sa_raw = pp_semantic_analyzer_create(parser.get());
    std::unique_ptr<PP_SemanticAnalyzer, decltype(&pp_semantic_analyzer_free)> sa(sa_raw, pp_semantic_analyzer_free);
    ASSERT_NE(sa.get(), nullptr);
    analyze_result = pp_semantic_analyzer_analyze(sa.get());
    semantic_errors = sa->error_list ? (int)sa->error_list->n_errors : 0;
}

TEST(SemanticTest, Pass1TypedefValid) {
    const char* source =
        "typedef int32 Handle;\n"
        "newtype float Distance;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Pass 1 should succeed without semantic errors for valid typedef/newtype
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, Pass1TypedefConstBaseError) {
    const char* source =
        "typedef const int32 BadHandle;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Base type cannot be const or reference -> should report semantic error
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, Pass1NewtypeRefBaseError) {
    const char* source =
        "newtype int32& BadNew;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, Pass1FunctionDeclaration) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    return 0;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, Pass1StructDeclaration) {
    const char* source =
        "struct Vec {\n"
        "    x :: float;\n"
        "    y :: float;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, Pass1LambdaDeclaration) {
    const char* source =
        "lambda add :: (a :: int32, b :: int32) -> int32 {\n"
        "    return a + b;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, Pass1VariableDeclaration) {
    const char* source =
        "func foo :: () -> int32 {\n"
        "    let x :: int32 = 5;\n"
        "    return x;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Debug print errors
    {
        auto lexer_h = pp_lexer_create(source, nullptr);
        std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
        pp_lexer_analyze(lexer.get());
        PP_Parser* parser_raw = pp_parser_create(lexer.get());
        std::unique_ptr<PP_Parser, decltype(&pp_parser_free)> parser(parser_raw, pp_parser_free);
        pp_parser_parse(parser.get());
        PP_SemanticAnalyzer* sa_raw = pp_semantic_analyzer_create(parser.get());
        std::unique_ptr<PP_SemanticAnalyzer, decltype(&pp_semantic_analyzer_free)> sa(sa_raw, pp_semantic_analyzer_free);
        pp_semantic_analyzer_analyze(sa.get());
        if (sa->error_list) {
            for (size_t i=0;i<sa->error_list->n_errors;i++) {
                printf("SEMERR %zu: %s at %d:%d\n", i, sa->error_list->errors[i].message, sa->error_list->errors[i].line, sa->error_list->errors[i].column);
            }
        }
    }
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, Pass1GlobalVariableReferenceError) {
    const char* source =
        "let g :: int32&;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Global variables cannot be references
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, Pass1TypedefDuplicateNameError) {
    const char* source =
        "typedef int32 Handle;\n"
        "typedef float Handle;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Duplicate typedef name should be reported
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, Pass1NewtypeDuplicateNameError) {
    const char* source =
        "newtype int32 Id;\n"
        "newtype float Id;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Duplicate newtype name should be reported
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, Pass1FunctionDuplicateNameError) {
    const char* source =
        "func foo :: () -> void {}\n"
        "func foo :: () -> void {}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Duplicate function name should be reported in Pass 1
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, Pass1StructDuplicateNameError) {
    const char* source =
        "struct S { x :: int32; }\n"
        "struct S { y :: int32; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Duplicate struct name should be reported in Pass 1
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, Pass2StructDuplicateFieldError) {
    const char* source =
        "struct S { x :: int32; x :: float; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Duplicate field name should be reported in Pass 2
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, Pass2GlobalVariableConstNoInitializerError) {
    const char* source =
        "let c :: const int32;\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Const global variable without initializer should be an error
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, Pass2GlobalVariableNonConstexprInitializerError) {
    const char* source =
        "func foo :: () -> int32 { return 1; }\n"
        "let g :: int32 = foo();\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Global variable initialized with non-constexpr expression should be an error
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, Pass2FunctionParamDuplicateError) {
    const char* source =
        "func foo :: (a :: int32, a :: float) -> void {}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Duplicate parameter names should be reported in Pass 2
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, FunctionCallParamCountMismatch) {
    const char* source =
        "func foo :: (a :: int32) -> int32 { return a; }\n"
        "func main :: () -> int32 { return foo(); }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Wrong argument count should be reported
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, FunctionCallCorrect) {
    const char* source =
        "func foo :: (a :: int32) -> int32 { return a; }\n"
        "func main :: () -> int32 { return foo(1); }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, LambdaCallParamCountMismatch) {
    const char* source =
        "lambda add :: (a :: int32, b :: int32) -> int32 { return a + b; }\n"
        "func main :: () -> int32 { return add(1); }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Wrong argument count for lambda should be reported
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, LambdaCallCorrect) {
    const char* source =
        "lambda add :: (a :: int32, b :: int32) -> int32 { return a + b; }\n"
        "func main :: () -> int32 { return add(1, 2); }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, LambdaMutableReferenceParamError) {
    const char* source =
        "lambda bad :: (x :: int32&) -> int32 { return x; }\n"
        "func main :: () -> int32 { let v :: int32 = 1; return bad(v); }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Lambda requires immutable reference parameters
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, LambdaVoidReturnError) {
    const char* source =
        "lambda bad :: () -> void { }\n"
        "func main :: () -> int32 { return bad(); }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Lambda forbids returning void
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, StructInitializerValid) {
    const char* source =
        "struct Point { x :: int32; y :: int32; }\n"
        "func main :: () -> int32 { let p :: Point = Point { .x = 1, .y = 2 }; return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, StructInitializerDuplicateFieldError) {
    const char* source =
        "struct Point { x :: int32; y :: int32; }\n"
        "func main :: () -> int32 { let p :: Point = Point { .x = 1, .x = 2 }; return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, StructInitializerUnknownFieldError) {
    const char* source =
        "struct Point { x :: int32; y :: int32; }\n"
        "func main :: () -> int32 { let p :: Point = Point { .z = 1 }; return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, StructInitializerTooManyFieldsError) {
    const char* source =
        "struct Point { x :: int32; y :: int32; }\n"
        "func main :: () -> int32 { let p :: Point = Point { .x = 1, .y = 2, .x = 3 }; return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Duplicate triggers error before count, but entry_count > field_count also errors
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, StructInitializerConstFieldMissingError) {
    const char* source =
        "struct S { a :: const int32; b :: int32; }\n"
        "func main :: () -> int32 { let s :: S = S { .b = 1 }; return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, StructInitializerReferenceFieldLvalueValid) {
    const char* source =
        "struct Vec { x :: int32&; }\n"
        "func main :: () -> int32 { let a :: int32 = 5; let v :: Vec = Vec { .x = a }; return a; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, StructInitializerReferenceFieldNonLvalueError) {
    const char* source =
        "struct Vec { x :: int32&; }\n"
        "func main :: () -> int32 { let v :: Vec = Vec { .x = 5 }; return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, StructCycleDirectSelfReference) {
    const char* source =
        "struct S { s :: S; }\n"
        "func main :: () -> int32 { return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, StructCycleIndirect) {
    const char* source =
        "struct A { b :: B; }\n"
        "struct B { a :: A; }\n"
        "func main :: () -> int32 { return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, StructCycleViaArray) {
    const char* source =
        "struct A { a :: [A : 1u]; }\n"
        "func main :: () -> int32 { return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, StructCycleAllowedViaList) {
    const char* source =
        "struct Node { next :: Node&; }\n"
        "func main :: () -> int32 { return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, StructCycleViaConstArray) {
    const char* source =
        "struct A { a :: const [A : 1u]; }\n"
        "func main :: () -> int32 { return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, StructCycleViaNewtype) {
    const char* source =
        "struct A { x :: B; }\n"
        "newtype A B;\n"
        "func main :: () -> int32 { return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}


