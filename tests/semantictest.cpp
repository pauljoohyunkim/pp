#include <gtest/gtest.h>
#include <memory>
#include "pp_lexer.h"
#include "pp_string_pool.h"
#include "pp_parser.h"
#include "pp_semantic_analyzer.h"

static void semantic_analyze_source(const char* source, int& lexer_errors, int& parser_errors, int& semantic_errors, int& analyze_result) {
    PP_StringPool* sp = pp_string_pool_create();
    lexer_errors = parser_errors = semantic_errors = 0;
    analyze_result = 0;
    {
        auto lexer_h = pp_lexer_create(source, nullptr, sp);
        std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
        ASSERT_NE(lexer.get(), nullptr);
        int lex_res = pp_lexer_analyze(lexer.get());
        lexer_errors = lexer->error_list ? (int)lexer->error_list->n_errors : 0;
        if (lex_res != 1) {
            return;
        }
        PP_Parser* parser_raw = pp_parser_create(lexer.get(), sp);
        std::unique_ptr<PP_Parser, decltype(&pp_parser_free)> parser(parser_raw, pp_parser_free);
        ASSERT_NE(parser.get(), nullptr);
        int parse_res = pp_parser_parse(parser.get());
        parser_errors = parser->error_list ? (int)parser->error_list->n_errors : 0;
        if (parse_res != 1) {
            return;
        }
        PP_SemanticAnalyzer* sa_raw = pp_semantic_analyzer_create(parser.get(), sp);
        std::unique_ptr<PP_SemanticAnalyzer, decltype(&pp_semantic_analyzer_free)> sa(sa_raw, pp_semantic_analyzer_free);
        ASSERT_NE(sa.get(), nullptr);
        analyze_result = pp_semantic_analyzer_analyze(sa.get());
        semantic_errors = sa->error_list ? (int)sa->error_list->n_errors : 0;
    }
    pp_string_pool_free(sp);
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
        PP_StringPool* sp = pp_string_pool_create();
        {
            auto lexer_h = pp_lexer_create(source, nullptr, sp);
            std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
            pp_lexer_analyze(lexer.get());
            PP_Parser* parser_raw = pp_parser_create(lexer.get(), sp);
            std::unique_ptr<PP_Parser, decltype(&pp_parser_free)> parser(parser_raw, pp_parser_free);
            pp_parser_parse(parser.get());
            PP_SemanticAnalyzer* sa_raw = pp_semantic_analyzer_create(parser.get(), sp);
            std::unique_ptr<PP_SemanticAnalyzer, decltype(&pp_semantic_analyzer_free)> sa(sa_raw, pp_semantic_analyzer_free);
            pp_semantic_analyzer_analyze(sa.get());
            if (sa->error_list) {
                for (size_t i=0;i<sa->error_list->n_errors;i++) {
                    printf("SEMERR %zu: %s at %d:%d\n", i, sa->error_list->errors[i].message, sa->error_list->errors[i].loc.line, sa->error_list->errors[i].loc.column);
                }
            }
        }
        pp_string_pool_free(sp);
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

TEST(SemanticTest, IllegalElementReferenceArray) {
    const char* source =
        "struct A { a :: [int32& : 4u]; }\n"
        "func main :: () -> int32 { return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, NestedArrayTypeValid) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: [[int32 : 3u] : 4u];\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, ArrayTransitiveReferenceBan) {
    const char* source =
        "struct S { x :: int32&; }\n"
        "func main :: () -> int32 {\n"
        "    let a :: [S : 4u];\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Array element type S contains a reference, so it should be rejected.
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, ListTransitiveReferenceBan) {
    const char* source =
        "struct S { x :: int32&; }\n"
        "func main :: () -> int32 {\n"
        "    let a :: [S];\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // List element type S contains a reference, so it should be rejected.
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, CollectionInitializerSuccessCriteria) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: [int32 : 3u];\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, CollectionInitializerWithLiterals) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: [int32 : 3u] = [int32 : 3u; 1, 2, 3];\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, CollectionInitializerListWithLiterals) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: [int32] = [int32; 10, 20, 30];\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, CollectionInitializerStructWithAlias) {
    const char* source =
        "typedef float myfloat;\n"
        "struct S { a :: int32; b :: myfloat; }\n"
        "func main :: () -> int32 {\n"
        "    let arr :: [S : 2u] = [S : 2u; S { .a = 1, .b = 2.0 }, S { .a = 3, .b = 4.0 }];\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, CollectionInitializerNestedCollections) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: [[int32 : 2u]] = [[int32 : 2u]; [int32 : 2u; 1, 2], [int32 : 2u; 3, 4]];\n"
        "    let b :: [[int32] : 2u] = [[int32] : 2u; [int32; 1, 2], [int32; 3]];\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, CollectionInitializerOversizeError) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: [int32 : 2u] = [int32 : 2u; 1, 2, 3];\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, CollectionInitializerUndersizeAllowed) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: [int32 : 4u] = [int32 : 4u; 1, 2, 3];\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, MemberAccessValid) {
    const char* source =
        "struct Point { x :: int32; y :: int32; }\n"
        "func main :: () -> int32 { let p :: Point = Point { .x = 1, .y = 2 }; let v :: int32 = p.x; return v; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, MemberAccessUnknownFieldError) {
    const char* source =
        "struct Point { x :: int32; y :: int32; }\n"
        "func main :: () -> int32 { let p :: Point = Point { .x = 1, .y = 2 }; let v :: int32 = p.z; return v; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, MemberAccessNewtypeError) {
    const char* source =
        "struct Point { x :: int32; }\n"
        "newtype int32 Vec;\n"
        "func main :: (v :: Vec) -> int32 { let x :: int32 = v.x; return x; }\n";
    // Member access on a newtype whose underlying type is not a struct should be rejected
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Expect semantic error for member access on non-struct type
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, ArrayAccessValid) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: [int32 : 3u] = [int32 : 3u; 1, 2, 3];\n"
        "    let v :: int32 = a[0u];\n"
        "    return v;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, ArrayAccessInvalidBase) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: int32 = 1;\n"
        "    let v :: int32 = a[0u];\n"
        "    return v;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, ArrayAccessNonIntegralIndex) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: [int32 : 3u] = [int32 : 3u; 1, 2, 3];\n"
        "    let i :: float = 1.0;\n"
        "    let v :: int32 = a[i];\n"
        "    return v;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, ListAccessValid) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: [int32] = [int32; 1, 2];\n"
        "    let v :: int32 = a[0u];\n"
        "    return v;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, ArrayAccessOutOfBounds) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: [int32 : 3u] = [int32 : 3u; 1, 2, 3];\n"
        "    let v :: int32 = a[3u];\n"
        "    return v;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, BinaryConstArithmetic) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: int32 = 1 + 2;\n"
        "    let b :: int32 = a + 3;\n"
        "    return b;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, BinaryConstArithmeticMismatchedTypes) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: int32 = 1;\n"
        "    let b :: float = 2.0;\n"
        "    let c :: int32 = a + b;\n"
        "    return c;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, NewtypeChainNominalDistinct) {
    const char* source =
        "newtype int32 AGE;\n"
        "newtype AGE WEIGHT;\n"
        "func main :: () -> int32 {\n"
        "    let a :: AGE = 5 as AGE;\n"
        "    let w :: WEIGHT = a;\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0); // nominal mismatch should error
}

TEST(SemanticTest, NewtypeChainCastAllowed) {
    const char* source =
        "newtype int32 AGE;\n"
        "newtype AGE WEIGHT;\n"
        "func main :: () -> int32 {\n"
        "    let a :: AGE = 5 as AGE;\n"
        "    let w :: WEIGHT = a as WEIGHT;\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, NewtypeChainCastToUnderlying) {
    const char* source =
        "newtype int32 AGE;\n"
        "newtype AGE WEIGHT;\n"
        "func main :: () -> int32 {\n"
        "    let w :: WEIGHT = 5 as WEIGHT;\n"
        "    let i :: int32 = w as int32;\n"
        "    return i;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, CastToConstTargetRejected) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let x :: int32 = 5 as const int32;\n"
        "    return x;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0); // casting to const target should error
}

TEST(SemanticTest, CastToReferenceTargetRejected) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let x :: int32 = 5 as int32&;\n"
        "    return x;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0); // casting to reference target should error
}

TEST(SemanticTest, MemberAccessConstPropagation) {
    const char* source =
        "struct Point { x :: int32; y :: int32; }\n"
        "func main :: () -> int32 {\n"
        "    let p :: const Point = Point { .x = 1, .y = 2 };\n"
        "    let v :: const int32 = p.x;\n"
        "    return v;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, MemberAccessConstViolation) {
    const char* source =
        "struct Point { x :: int32; y :: int32; }\n"
        "func main :: () -> int32 {\n"
        "    let p :: const Point = Point { .x = 1, .y = 2 };\n"
        "    p.x = 10;\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Assignment to member of const struct should be error
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, ArrayAccessConstPropagation) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: const [int32 : 3u] = [int32 : 3u; 1, 2, 3];\n"
        "    let v :: const int32 = a[0u];\n"
        "    return v;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, ArrayAccessConstViolation) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: const [int32 : 3u] = [int32 : 3u; 1, 2, 3];\n"
        "    a[0u] = 10;\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Assignment to element of const array should be error
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, NewtypeChainThreeLevels) {
    const char* source =
        "newtype int32 A;\n"
        "newtype A B;\n"
        "newtype B C;\n"
        "func main :: () -> int32 {\n"
        "    let a :: A = 1 as A;\n"
        "    let c :: C = a as C;\n"
        "    let i :: int32 = c as int32;\n"
        "    return i;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, LambdaCallPurityError) {
    const char* source =
        "func foo :: () -> int32 { return 1; }\n"
        "lambda bar :: () -> int32 { return foo(); }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Lambda body may only call other lambdas
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, LambdaCaptureMutableGlobalError) {
    const char* source =
        "let g :: int32 = 42;\n"
        "lambda bar :: () -> int32 { return g; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Lambda cannot capture mutable global
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, GlobalScopeCallError) {
    const char* source =
        "func foo :: () -> int32 { return 1; }\n"
        "let x :: int32 = foo();\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    // Function calls are not allowed at global scope
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, HashLengthOperatorValid) {
    const char* source =
        "func main :: () -> uint32 {\n"
        "    let arr :: [int32 : 5u];\n"
        "    let lst :: [int32];\n"
        "    let a :: uint32 = #arr;\n"
        "    let b :: uint32 = #lst;\n"
        "    return a;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, HashLengthOperatorInvalid) {
    const char* source =
        "func main :: () -> uint32 {\n"
        "    let x :: int32 = 5;\n"
        "    let y :: uint32 = #x;\n"
        "    return y;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, IntegerLiteralSuffixTypes) {
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
        "    let i :: int16 = 5s;\n"
        "    let j :: uint16 = 5us;\n"
        "    return 0;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, IntegerLiteralSuffixTypeMismatch) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: int8 = 1s;\n"   // 1s is int16, mismatch
        "    return 0;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, IntegerLiteralSuffixTripleL) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: int32 = 1lll;\n"
        "    return 0;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, IntegerLiteralSuffixCaseInsensitive) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: int16 = 1S;\n"
        "    let b :: uint16 = 1US;\n"
        "    let c :: int8 = 2SS;\n"
        "    let d :: uint8 = 2USS;\n"
        "    let e :: int64 = 3L;\n"
        "    let f :: uint64 = 3UL;\n"
        "    let g :: int128 = 4LL;\n"
        "    let h :: uint128 = 4ULL;\n"
        "    return 0;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, IntegerLiteralSuffixBinaryOps) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: int16 = 1s + 2s;\n"
        "    let b :: uint16 = 1us + 2us;\n"
        "    let c :: int8 = 1ss - 2ss;\n"
        "    let d :: uint8 = 1uss * 2uss;\n"
        "    let e :: int64 = 1l + 2l;\n"
        "    let f :: uint64 = 1ul * 2ul;\n"
        "    let g :: int128 = 1ll + 2ll;\n"
        "    let h :: uint128 = 1ull * 2ull;\n"
        "    return 0;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, IntegerLiteralSuffixUnaryOps) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: int16 = -1s;\n"
        "    let b :: uint16 = 1us;\n"
        "    let c :: int8 = -1ss;\n"
        "    let d :: int64 = -1l;\n"
        "    let e :: int128 = -1ll;\n"
        "    return 0;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, IntegerLiteralSuffixCompoundAssign) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: int16 = 0s;\n"
        "    a += 1s;\n"
        "    let b :: uint16 = 0us;\n"
        "    b -= 1us;\n"
        "    let c :: int8 = 0ss;\n"
        "    c *= 2ss;\n"
        "    let d :: int64 = 0l;\n"
        "    d += 1l;\n"
        "    let e :: int128 = 0ll;\n"
        "    e += 1ll;\n"
        "    return 0;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, IntegerLiteralSuffixBinaryTypeMismatch) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: int8 = 1s + 1ss;\n"   // int16 + int8 mismatch
        "    return 0;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, IntegerLiteralSuffixUnaryMinusUnsigned) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let a :: uint16 = -1us;\n"
        "    return 0;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}



TEST(SemanticTest, ConstReferenceRvalueBindingValid) {
    const char* source =
        "extern func print_int :: (v :: const int32&) -> void;\n"
        "func main :: () -> int32 { print_int(42); return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, MutableReferenceRvalueBindingError) {
    const char* source =
        "extern func write_int :: (v :: int32&) -> void;\n"
        "func main :: () -> int32 { write_int(42); return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, OperatorMutablePassModeRejected) {
    const char* source =
        "operator + :: (x :: int32&, y :: const int32&) -> int32 { return x; }\n"
        "func main :: () -> int32 { return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, OperatorAliasCollisionRejected) {
    const char* source =
        "typedef int32 my_int;\n"
        "operator + :: (x :: const int32&, y :: const int32&) -> int32 { return x; }\n"
        "operator + :: (x :: const my_int&, y :: const my_int&) -> my_int { return x; }\n"
        "func main :: () -> int32 { return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, OperatorReferenceReturnRejected) {
    const char* source =
        "struct S { x :: int32; }\n"
        "operator + :: (a :: const S&, b :: const S&) -> const S& { return a; }\n"
        "func main :: () -> int32 { return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, OperatorBuiltinStringConcatRejected) {
    const char* source =
        "operator + :: (a :: const string&, b :: const string&) -> string { return a; }\n"
        "func main :: () -> int32 { return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, OperatorValidOverloadDifferentTypes) {
    const char* source =
        "struct A { x :: int32; }\n"
        "struct B { x :: int32; }\n"
        "operator + :: (a :: const A&, b :: const A&) -> A { return A { .x = 0 }; }\n"
        "operator + :: (a :: const A&, b :: const B&) -> B { return B { .x = 0 }; }\n"
        "func main :: () -> int32 { return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
    EXPECT_EQ(analyze_result, 1);
}

TEST(SemanticTest, PointerAssignmentConstSubtypingAllowed) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let p1 :: ptr[int32] = nullptr[int32];\n"
        "    let p2 :: ptr[const int32] = nullptr[const int32];\n"
        "    p2 = p1;\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, PointerAssignmentConstStrippingRejected) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let p1 :: ptr[const int32] = nullptr[const int32];\n"
        "    let p2 :: ptr[int32] = nullptr[int32];\n"
        "    p2 = p1;\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, PointerToPointerCastConstSubtypingAllowed) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let p :: ptr[int32] = nullptr[int32];\n"
        "    let q :: ptr[const int32] = p as ptr[const int32];\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, PointerToPointerCastConstStrippingRejected) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let p :: ptr[const int32] = nullptr[const int32];\n"
        "    let q :: ptr[int32] = p as ptr[int32];\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, ReferenceToPointerCastAllowed) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let x :: int32 = 5;\n"
        "    let r :: int32& = x;\n"
        "    let p :: ptr[int32] = r as ptr[int32];\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, PointerToReferenceCastBanned) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let p :: ptr[int32] = nullptr[int32];\n"
        "    let r :: int32& = p as int32&;\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, AssignmentToConstReferenceRejected) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let x :: int32 = 1;\n"
        "    let y :: const int32& = x;\n"
        "    y = 2;\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, CollectionElementConstSubtypingAllowed) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let b :: [ptr[int32] : 2u] = [ptr[int32] : 2u; nullptr[int32], nullptr[int32]];\n"
        "    let a :: [ptr[const int32] : 2u] = [ptr[const int32] : 2u; nullptr[const int32], nullptr[const int32]];\n"
        "    a = b;\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

