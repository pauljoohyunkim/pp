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

TEST(SemanticTest, ListPushValid) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let v :: [int32];\n"
        "    let x :: int32 = 3;\n"
        "    list_push(v, x);\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, ListPopValid) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let v :: [int32] = [int32; 1, 2];\n"
        "    let x :: int32 = list_pop(v);\n"
        "    return x;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, ListClearValid) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let v :: [int32] = [int32; 1, 2];\n"
        "    list_clear(v);\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, ListPushWrongType) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let v :: [int32] = [int32; 1, 2];\n"
        "    list_push(v, true);\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, ListPushConstList) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let v :: const [int32] = [int32; 1, 2];\n"
        "    list_push(v, 3);\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, ListPushRefList) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let v :: [int32];\n"
        "    let r :: [int32]& = v;\n"
        "    let x :: int32 = 4;\n"
        "    list_push(r, x);\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, ListPopRefList) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let v :: [int32];\n"
        "    let r :: [int32]& = v;\n"
        "    let x :: int32 = list_pop(r);\n"
        "    return x;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, ListClearRefList) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let v :: [int32];\n"
        "    let r :: [int32]& = v;\n"
        "    list_clear(r);\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, ListPushConstRefList) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let v :: const [int32] = [int32; 1, 2];\n"
        "    let r :: const [int32]& = v;\n"
        "    list_push(r, 3);\n"
        "    return 0;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(analyze_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, OperatorBinaryBuiltinRedefinitionRejected) {
    const char* source =
        "operator + :: (x :: const int32&, y :: const int32&) -> int32 { return 99; }\n"
        "func main :: () -> int32 { return 1 + 2; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, OperatorUnaryBuiltinRedefinitionRejected) {
    const char* source =
        "operator - :: (x :: const int32&) -> int32 { return 100 - x; }\n"
        "func main :: () -> int32 { return -5; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}


/* ---------------------------------------------------------------------------
 * First-class function / lambda values (Phase 10).
 * ------------------------------------------------------------------------- */

static void expect_semantic_accept(const char* source, const char* label) {
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0) << label << "\nsource:\n" << source;
    EXPECT_EQ(semantic_errors, 0) << label << "\nsource:\n" << source;
    EXPECT_EQ(analyze_result, 1) << label << "\nsource:\n" << source;
}

static void expect_semantic_reject(const char* source, const char* label) {
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors + parser_errors, 0) << label << "\nsource:\n" << source;
    EXPECT_TRUE(semantic_errors > 0 || analyze_result == 0) << label << "\nsource:\n" << source;
}

TEST(SemanticTest, CallableVariableFromNamedFunctionAndCall) {
    expect_semantic_accept(
        "func add :: (a :: int32, b :: int32) -> int32 { return a + b; }\n"
        "func main :: () -> int32 {\n"
        "    let f :: func[(int32, int32) -> int32] = add;\n"
        "    return f(1, 2);\n"
        "}\n",
        "callable variable initialized from a function name and called");
}

TEST(SemanticTest, CallableVariableRebinding) {
    expect_semantic_accept(
        "func add :: (a :: int32, b :: int32) -> int32 { return a + b; }\n"
        "func sub :: (a :: int32, b :: int32) -> int32 { return a - b; }\n"
        "func main :: () -> int32 {\n"
        "    let f :: func[(int32, int32) -> int32] = add;\n"
        "    f = sub;\n"
        "    return f(1, 2);\n"
        "}\n",
        "rebinding a mutable callable variable");
}

TEST(SemanticTest, CallableConstBindingCannotBeRebound) {
    expect_semantic_reject(
        "func add :: (a :: int32) -> int32 { return a + 1; }\n"
        "func sub :: (a :: int32) -> int32 { return a - 1; }\n"
        "func main :: () -> int32 {\n"
        "    let c :: const func[(int32) -> int32] = add;\n"
        "    c = sub;\n"
        "    return 0;\n"
        "}\n",
        "const callable binding must not be rebindable");
}

TEST(SemanticTest, CallableReferenceAliasRebindsOriginal) {
    expect_semantic_accept(
        "func add :: (a :: int32) -> int32 { return a + 1; }\n"
        "func sub :: (a :: int32) -> int32 { return a - 1; }\n"
        "func main :: () -> int32 {\n"
        "    let a :: func[(int32) -> int32] = add;\n"
        "    let h :: func[(int32) -> int32]& = a;\n"
        "    h = sub;\n"
        "    return a(3);\n"
        "}\n",
        "reference to a callable variable aliases and rebinds the original");
}

TEST(SemanticTest, CallableExplicitLambdaToFuncWidening) {
    expect_semantic_accept(
        "lambda dbl :: (a :: int32) -> int32 { return a * 2; }\n"
        "func main :: () -> int32 {\n"
        "    let f :: func[(int32) -> int32] = dbl as func[(int32) -> int32];\n"
        "    return f(3);\n"
        "}\n",
        "explicit lambda -> func widening cast with an identical signature");
}

TEST(SemanticTest, CallableParameterAndReturnValue) {
    expect_semantic_accept(
        "func inc :: (a :: int32) -> int32 { return a + 1; }\n"
        "func apply :: (g :: func[(int32) -> int32], x :: int32) -> int32 { return g(x); }\n"
        "func pick :: () -> func[(int32) -> int32] { return inc; }\n"
        "func main :: () -> int32 {\n"
        "    return apply(pick(), 4);\n"
        "}\n",
        "callable parameter and callable return value");
}

TEST(SemanticTest, CallableStructFieldRequiresInitializer) {
    expect_semantic_reject(
        "struct S { f :: func[(int32) -> int32]; }\n"
        "func main :: () -> int32 {\n"
        "    let s :: S = S { };\n"
        "    return 0;\n"
        "}\n",
        "callable struct field must be initialized");
}

TEST(SemanticTest, CallableStructFieldWithInitializer) {
    expect_semantic_accept(
        "func inc :: (a :: int32) -> int32 { return a + 1; }\n"
        "struct S { f :: func[(int32) -> int32]; }\n"
        "func main :: () -> int32 {\n"
        "    let s :: S = S { .f = inc };\n"
        "    return s.f(10);\n"
        "}\n",
        "callable struct field initialized and called through the field");
}

TEST(SemanticTest, CallableGlobalFromFunctionName) {
    expect_semantic_accept(
        "func inc :: (a :: int32) -> int32 { return a + 1; }\n"
        "let g :: func[(int32) -> int32] = inc;\n"
        "func main :: () -> int32 { return g(5); }\n",
        "callable global initialized from a function name");
}

TEST(SemanticTest, CallableHigherOrderSignature) {
    expect_semantic_accept(
        "typedef func[(int32) -> int32] Unary;\n"
        "typedef func[() -> Unary] Factory;\n"
        "func main :: () -> int32 { return 0; }\n",
        "higher-order callable signature");
}

TEST(SemanticTest, CallableFunctionNameIsNotAStorageLocation) {
    expect_semantic_reject(
        "func add :: (a :: int32) -> int32 { return a + 1; }\n"
        "func main :: () -> int32 {\n"
        "    let h :: func[(int32) -> int32]& = add;\n"
        "    return 0;\n"
        "}\n",
        "a function name is not an l-value and cannot be bound by reference");
}

TEST(SemanticTest, CallableUninitializedVariableRejected) {
    expect_semantic_reject(
        "func main :: () -> int32 {\n"
        "    let f :: func[(int32) -> int32];\n"
        "    return 0;\n"
        "}\n",
        "callable local variable requires an initializer");
}

TEST(SemanticTest, CallableUninitializedGlobalRejected) {
    expect_semantic_reject(
        "let g :: func[(int32) -> int32];\n"
        "func main :: () -> int32 { return 0; }\n",
        "callable global variable requires an initializer");
}

TEST(SemanticTest, CallableCollectionsAllowed) {
    expect_semantic_accept(
        "func inc :: (a :: int32) -> int32 { return a + 1; }\n"
        "func dec :: (a :: int32) -> int32 { return a - 1; }\n"
        "func main :: () -> int32 {\n"
        "    let l :: [func[(int32) -> int32]] = [func[(int32) -> int32]; inc, dec];\n"
        "    let a :: [func[(int32) -> int32] : 2u] = [func[(int32) -> int32] : 2u; inc, dec];\n"
        "    return l[0u](1) + a[1u](1);\n"
        "}\n",
        "lists and arrays of callable values are supported");
}

TEST(SemanticTest, CallableGlobalNonEmptyListRejected) {
    expect_semantic_reject(
        "func inc :: (a :: int32) -> int32 { return a + 1; }\n"
        "let fs :: [func[(int32) -> int32]] = [func[(int32) -> int32]; inc];\n"
        "func main :: () -> int32 { return 0; }\n",
        "a non-empty global list is still rejected, including lists of callables");
}

TEST(SemanticTest, CallablePointerToFunctionAllowed) {
    expect_semantic_accept(
        "func inc :: (a :: int32) -> int32 { return a + 1; }\n"
        "func main :: () -> int32 {\n"
        "    let f :: func[(int32) -> int32] = inc;\n"
        "    let p :: ptr[func[(int32) -> int32]] = @f;\n"
        "    let g :: func[(int32) -> int32] = $p;\n"
        "    let q :: ptr[func[(int32) -> int32]] = nullptr[func[(int32) -> int32]];\n"
        "    if (q == nullptr[func[(int32) -> int32]]) { return g(1); }\n"
        "    return 0;\n"
        "}\n",
        "pointers to func[...] values, nullptr and pointer comparison are supported");
}

TEST(SemanticTest, CallablePointerToLambdaRejected) {
    expect_semantic_reject(
        "func main :: () -> int32 {\n"
        "    let p :: ptr[lambda[(int32) -> int32]];\n"
        "    return 0;\n"
        "}\n",
        "ptr[lambda[...]] is rejected");
    expect_semantic_reject(
        "func main :: () -> int32 {\n"
        "    let p :: ptr[lambda[(int32) -> int32]] = nullptr[lambda[(int32) -> int32]];\n"
        "    return 0;\n"
        "}\n",
        "nullptr[lambda[...]] is rejected");
    expect_semantic_reject(
        "lambda dbl :: (a :: int32) -> int32 { return a * 2; }\n"
        "func main :: () -> int32 {\n"
        "    let l :: lambda[(int32) -> int32] = dbl;\n"
        "    @l;\n"
        "    return 0;\n"
        "}\n",
        "address-of a lambda value is rejected");
}

TEST(SemanticTest, CallableEqualityAllowed) {
    expect_semantic_accept(
        "func inc :: (a :: int32) -> int32 { return a + 1; }\n"
        "func dec :: (a :: int32) -> int32 { return a - 1; }\n"
        "func main :: () -> int32 {\n"
        "    let a :: func[(int32) -> int32] = inc;\n"
        "    let b :: func[(int32) -> int32] = dec;\n"
        "    let c :: func[(int32) -> int32] = a;\n"
        "    if (a == b || a != c) { return 1; }\n"
        "    return 0;\n"
        "}\n",
        "== and != between identical callable types are supported");
}

TEST(SemanticTest, CallableEqualityRequiresIdenticalType) {
    expect_semantic_reject(
        "func inc :: (a :: int32) -> int32 { return a + 1; }\n"
        "func inc64 :: (a :: int64) -> int32 { return 0; }\n"
        "func main :: () -> int32 {\n"
        "    let a :: func[(int32) -> int32] = inc;\n"
        "    let b :: func[(int64) -> int32] = inc64;\n"
        "    if (a == b) { return 1; }\n"
        "    return 0;\n"
        "}\n",
        "comparison of callables with different signatures is rejected");
    expect_semantic_reject(
        "func inc :: (a :: int32) -> int32 { return a + 1; }\n"
        "lambda dbl :: (a :: int32) -> int32 { return a * 2; }\n"
        "func main :: () -> int32 {\n"
        "    let a :: func[(int32) -> int32] = inc;\n"
        "    let b :: lambda[(int32) -> int32] = dbl;\n"
        "    if (a == b) { return 1; }\n"
        "    return 0;\n"
        "}\n",
        "comparison between func and lambda values is rejected");
}

TEST(SemanticTest, CallableNoImplicitLambdaWidening) {
    expect_semantic_reject(
        "lambda dbl :: (a :: int32) -> int32 { return a * 2; }\n"
        "func main :: () -> int32 {\n"
        "    let f :: func[(int32) -> int32] = dbl;\n"
        "    return 0;\n"
        "}\n",
        "implicit lambda -> func initialization is rejected");
    expect_semantic_reject(
        "lambda dbl :: (a :: int32) -> int32 { return a * 2; }\n"
        "func apply :: (g :: func[(int32) -> int32], x :: int32) -> int32 { return g(x); }\n"
        "func main :: () -> int32 { return apply(dbl, 1); }\n",
        "implicit lambda -> func argument passing is rejected");
    expect_semantic_reject(
        "lambda dbl :: (a :: int32) -> int32 { return a * 2; }\n"
        "func make :: () -> func[(int32) -> int32] { return dbl; }\n"
        "func main :: () -> int32 { return 0; }\n",
        "implicit lambda -> func return is rejected");
}

TEST(SemanticTest, CallableMismatchedSignatureRejected) {
    expect_semantic_reject(
        "func add :: (a :: int32) -> int32 { return a + 1; }\n"
        "func main :: () -> int32 {\n"
        "    let f :: func[(int32, int32) -> int32] = add;\n"
        "    return 0;\n"
        "}\n",
        "callable with a mismatched signature is rejected");
}

TEST(SemanticTest, CallableCastRestrictions) {
    expect_semantic_reject(
        "func add :: (a :: int32) -> int32 { return a + 1; }\n"
        "func main :: () -> int32 {\n"
        "    let l :: lambda[(int32) -> int32] = add as lambda[(int32) -> int32];\n"
        "    return 0;\n"
        "}\n",
        "func -> lambda cast is rejected");
    expect_semantic_reject(
        "func add :: (a :: int32) -> int32 { return a + 1; }\n"
        "func main :: () -> int32 {\n"
        "    let n :: int32 = add as int32;\n"
        "    return 0;\n"
        "}\n",
        "callable -> non-callable cast is rejected");
    expect_semantic_reject(
        "lambda dbl :: (a :: int32) -> int32 { return a * 2; }\n"
        "func main :: () -> int32 {\n"
        "    let f :: func[(int32, int32) -> int32] = dbl as func[(int32, int32) -> int32];\n"
        "    return 0;\n"
        "}\n",
        "widening cast with a mismatched signature is rejected");
}

TEST(SemanticTest, CallableInsideLambdaPurityRules) {
    expect_semantic_accept(
        "lambda dbl :: (a :: int32) -> int32 { return a * 2; }\n"
        "lambda applyL :: (g :: lambda[(int32) -> int32], x :: int32) -> int32 { return g(x); }\n"
        "func main :: () -> int32 { return 0; }\n",
        "calling a lambda-typed value inside a lambda is allowed");
    expect_semantic_reject(
        "lambda applyF :: (g :: func[(int32) -> int32], x :: int32) -> int32 {\n"
        "    return g(x);\n"
        "}\n"
        "func main :: () -> int32 { return 0; }\n",
        "calling a func-typed value inside a lambda is rejected");
}

TEST(SemanticTest, CallableOperatorsRejected) {
    expect_semantic_reject(
        "func add :: (a :: int32) -> int32 { return a + 1; }\n"
        "func other :: (a :: int32) -> int32 { return a + 2; }\n"
        "func main :: () -> int32 {\n"
        "    let a :: func[(int32) -> int32] = add;\n"
        "    let b :: func[(int32) -> int32] = other;\n"
        "    let c :: func[(int32) -> int32] = a + b;\n"
        "    return 0;\n"
        "}\n",
        "arithmetic on callable values is rejected");
    expect_semantic_reject(
        "func add :: (a :: int32) -> int32 { return a + 1; }\n"
        "func main :: () -> int32 {\n"
        "    let a :: func[(int32) -> int32] = add;\n"
        "    let b :: bool = !a;\n"
        "    return 0;\n"
        "}\n",
        "unary operators other than '@' are rejected on callable values");
}

TEST(SemanticTest, CallableVoidParameterRejected) {
    expect_semantic_reject(
        "typedef func[(void) -> int32] Bad;\n"
        "func main :: () -> int32 { return 0; }\n",
        "void callable parameter is rejected");
}
TEST(SemanticTest, BreakOutsideLoop) {
    const char* source =
        "func main :: () -> int32 { break; return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, ContinueOutsideLoop) {
    const char* source =
        "func main :: () -> int32 { continue; return 0; }\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_GT(semantic_errors, 0);
}

TEST(SemanticTest, BreakInsideWhile) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let i :: int32 = 0;\n"
        "    while (i < 10) {\n"
        "        break;\n"
        "        i = i + 1;\n"
        "    }\n"
        "    return i;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, ContinueInsideFor) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let i :: int32 = 0;\n"
        "    for (; i < 10; i = i + 1) {\n"
        "        continue;\n"
        "    }\n"
        "    return i;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

TEST(SemanticTest, BreakInsideNestedLoops) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let i :: int32 = 0;\n"
        "    while (i < 10) {\n"
        "        let j :: int32 = 0;\n"
        "        while (j < 10) {\n"
        "            break;\n"
        "        }\n"
        "        i = i + 1;\n"
        "    }\n"
        "    return i;\n"
        "}\n";
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors, 0);
    EXPECT_EQ(parser_errors, 0);
    EXPECT_EQ(semantic_errors, 0);
}

/* ---------------------------------------------------------------------------
 * Handle declaration tests
 *
 * Expected outcome helpers: keep the handle tests terse.
 * ------------------------------------------------------------------------- */
static void expect_handle_ok(const char* source) {
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors, 0) << source;
    EXPECT_EQ(parser_errors, 0) << source;
    EXPECT_EQ(analyze_result, 1) << source;
    EXPECT_EQ(semantic_errors, 0) << source;
}

static void expect_handle_error(const char* source) {
    int lexer_errors = 0, parser_errors = 0, semantic_errors = 0, analyze_result = 0;
    semantic_analyze_source(source, lexer_errors, parser_errors, semantic_errors, analyze_result);
    EXPECT_EQ(lexer_errors, 0) << source;
    EXPECT_EQ(parser_errors, 0) << source;
    EXPECT_GT(semantic_errors, 0) << source;
}

TEST(SemanticTest, HandleDeclInt32Valid) {
    expect_handle_ok("handle File :: int32 drop pp_close;\n"
                     "extern func pp_close :: (fd :: int32) -> void;\n");
}

TEST(SemanticTest, HandleDeclPointerValid) {
    expect_handle_ok("handle Res :: ptr[void] drop pp_free;\n"
                     "extern func pp_free :: (p :: ptr[void]) -> void;\n");
}

TEST(SemanticTest, HandleDuplicateNameError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n");
}

TEST(SemanticTest, HandleCollidesWithNewtypeError) {
    expect_handle_error("newtype int32 File;\n"
                        "handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n");
}

TEST(SemanticTest, HandleConstUnderlyingError) {
    expect_handle_error("handle File :: const int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n");
}

TEST(SemanticTest, HandleReferenceUnderlyingError) {
    expect_handle_error("handle File :: int32& drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n");
}

TEST(SemanticTest, HandleStructUnderlyingWithReferenceError) {
    expect_handle_error("struct Holder { r :: int32&; }\n"
                        "handle File :: Holder drop pp_close;\n"
                        "extern func pp_close :: (h :: Holder) -> void;\n");
}

TEST(SemanticTest, HandleOfHandleError) {
    expect_handle_error("handle Inner :: int32 drop pp_close;\n"
                        "handle Outer :: Inner drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n");
}

TEST(SemanticTest, HandleFunctionUnderlyingError) {
    expect_handle_error("handle Fn :: func[(int32) -> int32] drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n");
}

TEST(SemanticTest, HandleMissingDropFunctionError) {
    expect_handle_error("handle File :: int32 drop pp_missing;\n");
}

TEST(SemanticTest, HandleDropWrongParamTypeError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (p :: ptr[void]) -> void;\n");
}

TEST(SemanticTest, HandleDropTwoParamsError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32, extra :: int32) -> void;\n");
}

TEST(SemanticTest, HandleDropNonVoidReturnError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> int32;\n");
}

TEST(SemanticTest, HandleDropLambdaError) {
    expect_handle_error("handle File :: int32 drop pp_release;\n"
                        "lambda pp_release :: (fd :: int32) -> void { return; }\n");
}

TEST(SemanticTest, HandleDropCallableVariableError) {
    expect_handle_error("handle File :: int32 drop rel;\n"
                        "let rel :: func[(int32) -> void] = nullptr[void];\n");
}

/* --- Handle positions: params, globals, fields, collections, casts --- */

TEST(SemanticTest, HandleByValueParameterError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n"
                        "func consume :: (f :: File) -> void { return; }\n");
}

TEST(SemanticTest, HandleConstRefParameterValid) {
    expect_handle_ok("handle File :: int32 drop pp_close;\n"
                     "extern func pp_close :: (fd :: int32) -> void;\n"
                     "func consume :: (f :: const File&) -> void { return; }\n"
                     "func main :: () -> int32 {\n"
                     "    let h :: File = pp_open();\n"
                     "    consume(h);\n"
                     "    return 0;\n"
                     "}\n"
                     "extern func pp_open :: () -> File;\n");
}

TEST(SemanticTest, HandleMutableRefParameterValid) {
    expect_handle_ok("handle File :: int32 drop pp_close;\n"
                     "extern func pp_close :: (fd :: int32) -> void;\n"
                     "extern func pp_open :: () -> File;\n"
                     "func use :: (f :: File&) -> void { return; }\n"
                     "func main :: () -> int32 {\n"
                     "    let h :: File = pp_open();\n"
                     "    use(h);\n"
                     "    return 0;\n"
                     "}\n");
}

TEST(SemanticTest, HandleLambdaParameterError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n"
                        "lambda peek :: (f :: const File&) -> int32 { return 0; }\n");
}

TEST(SemanticTest, HandleLambdaReturnError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n"
                        "lambda open_it :: () -> File { return pp_open(); }\n"
                        "extern func pp_open :: () -> File;\n");
}

TEST(SemanticTest, HandleGlobalVariableError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n"
                        "let g :: File = nullptr[File];\n");
}

TEST(SemanticTest, HandleStructFieldError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n"
                        "struct Holder { f :: File; }\n");
}

TEST(SemanticTest, HandleCollectionElementError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n"
                        "func main :: () -> int32 {\n"
                        "    let xs :: [File];\n"
                        "    return 0;\n"
                        "}\n");
}

TEST(SemanticTest, HandleCastToHandleError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n"
                        "func main :: () -> int32 {\n"
                        "    let h :: File = 0 as File;\n"
                        "    return 0;\n"
                        "}\n");
}

TEST(SemanticTest, HandleRequiresInitializerError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n"
                        "func main :: () -> int32 {\n"
                        "    let h :: File;\n"
                        "    return 0;\n"
                        "}\n");
}

TEST(SemanticTest, HandleReturnLocalError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n"
                        "extern func pp_open :: () -> File;\n"
                        "func make :: () -> File {\n"
                        "    let h :: File = pp_open();\n"
                        "    return h;\n"
                        "}\n");
}

TEST(SemanticTest, HandleReturnRvalueValid) {
    expect_handle_ok("handle File :: int32 drop pp_close;\n"
                     "extern func pp_close :: (fd :: int32) -> void;\n"
                     "extern func pp_open :: () -> File;\n"
                     "func make :: () -> File { return pp_open(); }\n");
}

TEST(SemanticTest, HandleReverseCastValid) {
    expect_handle_ok("handle File :: int32 drop pp_close;\n"
                     "extern func pp_close :: (fd :: int32) -> void;\n"
                     "extern func pp_open :: () -> File;\n"
                     "func main :: () -> int32 {\n"
                     "    let h :: File = pp_open();\n"
                     "    let raw :: int32 = h as int32;\n"
                     "    return raw;\n"
                     "}\n");
}

TEST(SemanticTest, HandleReassignmentValid) {
    expect_handle_ok("handle File :: int32 drop pp_close;\n"
                     "extern func pp_close :: (fd :: int32) -> void;\n"
                     "extern func pp_open :: () -> File;\n"
                     "func main :: () -> int32 {\n"
                     "    let h :: File = pp_open();\n"
                     "    h = pp_open();\n"
                     "    return 0;\n"
                     "}\n");
}

TEST(SemanticTest, HandleLocalInLambdaError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n"
                        "extern func pp_open :: () -> File;\n"
                        "lambda leaky :: () -> int32 {\n"
                        "    let h :: File = pp_open();\n"
                        "    return 0;\n"
                        "}\n");
}

TEST(SemanticTest, HandleLocalInBlockValid) {
    expect_handle_ok("handle File :: int32 drop pp_close;\n"
                     "extern func pp_close :: (fd :: int32) -> void;\n"
                     "extern func pp_open :: () -> File;\n"
                     "func main :: () -> int32 {\n"
                     "    {\n"
                     "        let h :: File = pp_open();\n"
                     "    }\n"
                     "    return 0;\n"
                     "}\n");
}

TEST(SemanticTest, HandleDropFunctionDirectCallError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n"
                        "func main :: () -> int32 {\n"
                        "    pp_close(1);\n"
                        "    return 0;\n"
                        "}\n");
}

TEST(SemanticTest, HandleDropFunctionNotCalledWhenUnrelatedNameValid) {
    expect_handle_ok("handle File :: int32 drop pp_close;\n"
                     "extern func pp_close :: (fd :: int32) -> void;\n"
                     "extern func pp_open :: () -> File;\n"
                     "func helper :: (fd :: int32) -> void { return; }\n"
                     "func main :: () -> int32 {\n"
                     "    helper(1);\n"
                     "    let h :: File = pp_open();\n"
                     "    return 0;\n"
                     "}\n");
}

TEST(SemanticTest, HandleDropFunctionAsValueError) {
    expect_handle_error("handle File :: int32 drop pp_close;\n"
                        "extern func pp_close :: (fd :: int32) -> void;\n"
                        "func main :: () -> int32 {\n"
                        "    let g :: func[(int32) -> void] = pp_close;\n"
                        "    g(1);\n"
                        "    return 0;\n"
                        "}\n");
}

TEST(SemanticTest, HandleDropTakingAnotherHandleError) {
    expect_handle_error("handle A :: int32 drop b_close;\n"
                        "handle B :: int32 drop b_close;\n"
                        "extern func b_close :: (b :: B) -> void;\n");
}

TEST(SemanticTest, HandleDropAsExternIsAccepted) {
    expect_handle_ok("handle File :: int32 drop pp_close;\n"
                     "extern func pp_close :: (fd :: int32) -> void;\n");
}

TEST(SemanticTest, HandleDropAsDefinedFuncIsAccepted) {
    expect_handle_ok("extern func pp_release :: (fd :: int32) -> void;\n"
                     "func pp_close :: (fd :: int32) -> void { pp_release(fd); }\n"
                     "handle File :: int32 drop pp_close;\n");
}

TEST(SemanticTest, CharLiteralTypeIsUint8) {
    expect_semantic_accept("func main :: () -> uint8 { let c :: uint8 = 'A'; return c; }", "char literal");
}

TEST(SemanticTest, HexLiteralTypeInference) {
    expect_semantic_accept("func main :: () -> uint32 { let v :: uint32 = 0x41u; return v; }", "hex literal");
}

TEST(SemanticTest, EnumDeclarationAccepted) {
    expect_semantic_accept("enum Color :: uint8 { Red, Green, Blue }\n"
                           "func main :: () -> uint8 {\n"
                           "    let c :: Color = Color::Green;\n"
                           "    return c as uint8;\n"
                           "}\n",
                           "enum declaration");
}

TEST(SemanticTest, EnumExplicitAndAutoValuesAccepted) {
    expect_semantic_accept("enum Status :: int32 { Ok = 10, Warn, Error = 100 }\n"
                           "func main :: () -> int32 {\n"
                           "    let s :: Status = Status::Error;\n"
                           "    return s as int32;\n"
                           "}\n",
                           "enum explicit and auto values");
}

TEST(SemanticTest, EnumEqualityComparisonAccepted) {
    expect_semantic_accept("enum Mode :: int32 { A, B }\n"
                           "func main :: () -> int32 {\n"
                           "    let m :: Mode = Mode::A;\n"
                           "    if (m == Mode::B) { return 1; }\n"
                           "    if (m != Mode::A) { return 2; }\n"
                           "    return 0;\n"
                           "}\n",
                           "enum equality");
}

TEST(SemanticTest, EnumConstantUsableInArraySizeAndGlobalInitializer) {
    expect_semantic_accept("enum Sizes :: uint32 { Small = 4, Large = 16 }\n"
                           "let buffer :: [uint8 : Sizes::Small];\n"
                           "let total :: Sizes = Sizes::Large;\n"
                           "func main :: () -> Sizes { return total; }\n",
                           "enum constant as array size and global initializer");
}

TEST(SemanticTest, FlagDeclarationAndOperationsAccepted) {
    expect_semantic_accept("flag Perm :: uint32 { Read = 1, Write = 2, Execute = 4 }\n"
                           "func main :: () -> uint32 {\n"
                           "    let p :: Perm = Perm::Read | Perm::Write;\n"
                           "    p |= Perm::Execute;\n"
                           "    p &= Perm::Read;\n"
                           "    p ^= Perm::Write;\n"
                           "    let q :: Perm = ~p;\n"
                           "    if ((p & Perm::Read) != Perm::none) { return 1u; }\n"
                           "    if (p == Perm::none) { return 2u; }\n"
                           "    return q as uint32;\n"
                           "}\n",
                           "flag declaration and operations");
}

TEST(SemanticTest, FlagAutoValuesAccepted) {
    expect_semantic_accept("flag F :: uint16 { A, B, C }\n"
                           "func main :: () -> uint16 { let f :: F = F::C; return f as uint16; }\n",
                           "flag auto values");
}

TEST(SemanticTest, FlagCompositeMemberAccepted) {
    expect_semantic_accept("flag Acc :: uint32 { Read = 1, Write = 2, ReadWrite = Read | Write }\n"
                           "func main :: () -> uint32 { let a :: Acc = Acc::ReadWrite; return a as uint32; }\n",
                           "flag composite member");
}

TEST(SemanticTest, FlagHeroExplicitValueAccepted) {
    expect_semantic_accept("flag Bits :: uint32 { Sync = 1 << 10, Next }\n"
                           "func main :: () -> uint32 { let b :: Bits = Bits::Next; return b as uint32; }\n",
                           "flag explicit high bit followed by auto");
}

TEST(SemanticTest, EnumCharLiteralValuesAccepted) {
    expect_semantic_accept("enum Cmd :: uint8 { Plus = '+', Minus = '-', Period = '.' }\n"
                           "func main :: () -> uint8 { return Cmd::Period as uint8; }\n",
                           "enum member values from char literals");
}

TEST(SemanticTest, EnumCharLiteralNonIncreasingRejected) {
    expect_semantic_reject("enum Cmd :: uint8 { Greater = '>', Less = '<' }\n"
                           "func main :: () -> uint8 { return 0 as uint8; }\n",
                           "enum char literal members out of order");
}

TEST(SemanticTest, FlagCharLiteralValuesAccepted) {
    expect_semantic_accept("flag Bits :: uint8 { Space = ' ', At = '@' }\n"
                           "func main :: () -> uint8 { return Bits::At as uint8; }\n",
                           "flag member values from char literals (single-bit code points)");
}

TEST(SemanticTest, TildeOnIntegerAccepted) {
    expect_semantic_accept("func main :: () -> uint32 { let v :: uint32 = 0u; return ~v; }\n",
                           "tilde on integer");
}

TEST(SemanticTest, CastBetweenEnumAndIntegerAccepted) {
    expect_semantic_accept("enum Mode :: int32 { A, B }\n"
                           "func main :: () -> int32 {\n"
                           "    let m :: Mode = Mode::B;\n"
                           "    let i :: int32 = m as int32;\n"
                           "    return i;\n"
                           "}\n",
                           "enum to integer cast");
}

TEST(SemanticTest, CastIntegerToFlagAccepted) {
    expect_semantic_accept("flag Perm :: uint32 { Read = 1, Write = 2 }\n"
                           "func main :: () -> uint32 {\n"
                           "    let p :: Perm = 3u as Perm;\n"
                           "    return p as uint32;\n"
                           "}\n",
                           "integer to flag cast");
}

TEST(SemanticTest, EnumDuplicateMemberRejected) {
    expect_semantic_reject("enum Mode :: int32 { A, B, A }\n", "duplicate enum member");
}

TEST(SemanticTest, EnumNonIncreasingValueRejected) {
    expect_semantic_reject("enum Mode :: int32 { A = 5, B = 3 }\n", "enum values strictly increasing");
}

TEST(SemanticTest, EnumValueOutOfRangeRejected) {
    expect_semantic_reject("enum Mode :: uint8 { A = 300 }\n", "enum value out of range");
}

TEST(SemanticTest, EnumUnderlyingMustBeInteger) {
    expect_semantic_reject("enum Mode :: float { A }\n", "enum underlying must be integer");
}

TEST(SemanticTest, FlagUnderlyingMustBeUnsigned) {
    expect_semantic_reject("flag Perm :: int32 { Read = 1 }\n", "flag underlying must be unsigned");
}

TEST(SemanticTest, FlagMemberNamedNoneRejected) {
    expect_semantic_reject("flag Perm :: uint32 { none = 1 }\n", "flag member named none");
}

TEST(SemanticTest, FlagExplicitZeroRejected) {
    expect_semantic_reject("flag Perm :: uint32 { Read = 0 }\n", "flag explicit zero");
}

TEST(SemanticTest, FlagArbitraryCompositeRejected) {
    expect_semantic_reject("flag Perm :: uint32 { Read = 1, Write = 2, Bad = 5 }\n",
                           "flag composite must be single bit or OR of earlier members");
}

TEST(SemanticTest, FlagArithmeticRejected) {
    expect_semantic_reject("flag Perm :: uint32 { Read = 1 }\n"
                           "func main :: () -> int32 {\n"
                           "    let p :: Perm = Perm::Read + Perm::Read;\n"
                           "    return 0;\n"
                           "}\n",
                           "flag arithmetic rejected");
}

TEST(SemanticTest, EnumArithmeticRejected) {
    expect_semantic_reject("enum Mode :: int32 { A, B }\n"
                           "func main :: () -> int32 {\n"
                           "    let m :: Mode = Mode::A + Mode::B;\n"
                           "    return 0;\n"
                           "}\n",
                           "enum arithmetic rejected");
}

TEST(SemanticTest, QualifiedConstantUnknownMemberRejected) {
    expect_semantic_reject("enum Mode :: int32 { A }\n"
                           "func main :: () -> int32 {\n"
                           "    let m :: Mode = Mode::Z;\n"
                           "    return 0;\n"
                           "}\n",
                           "unknown enum member");
}

TEST(SemanticTest, QualifiedConstantOnStructRejected) {
    expect_semantic_reject("struct S { x :: int32; }\n"
                           "func main :: () -> int32 {\n"
                           "    let s :: S = S::x;\n"
                           "    return 0;\n"
                           "}\n",
                           "qualified constant on non-enum type");
}

TEST(SemanticTest, CastIntegerToEnumRejected) {
    expect_semantic_reject("enum Mode :: int32 { A, B }\n"
                           "func main :: () -> int32 {\n"
                           "    let m :: Mode = 1 as Mode;\n"
                           "    return 0;\n"
                           "}\n",
                           "integer to enum cast rejected");
}

TEST(SemanticTest, CastBetweenDifferentEnumsRejected) {
    expect_semantic_reject("enum A :: int32 { X }\n"
                           "enum B :: int32 { Y }\n"
                           "func main :: () -> int32 {\n"
                           "    let b :: B = A::X as B;\n"
                           "    return 0;\n"
                           "}\n",
                           "cast between different enums rejected");
}

TEST(SemanticTest, EnumAndFlagMixedOperandsRejected) {
    expect_semantic_reject("enum A :: int32 { X }\n"
                           "flag F :: uint32 { Y = 1 }\n"
                           "func main :: () -> int32 {\n"
                           "    let a :: A = A::X;\n"
                           "    let f :: F = F::Y;\n"
                           "    if (a == f) { return 1; }\n"
                           "    return 0;\n"
                           "}\n",
                           "mixing enum and flag operands rejected");
}
