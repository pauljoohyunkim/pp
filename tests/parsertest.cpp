#include <gtest/gtest.h>
#include <memory>
#include "pp_lexer.h"
#include "pp_parser.h"

static void parse_source(const char* source, int& lexer_errors, int& parser_errors, int& parse_result) {
    auto lexer_h = pp_lexer_create(source, nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_NE(lexer.get(), nullptr);
    int analyze_res = pp_lexer_analyze(lexer.get());
    lexer_errors = lexer->error_list ? (int)lexer->error_list->n_errors : 0;
    if (analyze_res != 1) {
        parse_result = 0;
        parser_errors = 0;
        return;
    }
    PP_Parser* parser_raw = pp_parser_create(lexer.get());
    std::unique_ptr<PP_Parser, decltype(&pp_parser_free)> parser(parser_raw, pp_parser_free);
    ASSERT_NE(parser.get(), nullptr);
    parse_result = pp_parser_parse(parser.get());
    parser_errors = parser->error_list ? (int)parser->error_list->n_errors : 0;
}

TEST(ParserTest, GoodFunctionDeclaration) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let x :: int32 = 10;\n"
        "    return x;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodStructDeclaration) {
    const char* source =
        "struct Vec {\n"
        "    x :: float;\n"
        "    y :: float;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodVariableAndIf) {
    const char* source =
        "func check :: (v :: int32) -> bool {\n"
        "    if (v > 0) {\n"
        "        return true;\n"
        "    } else {\n"
        "        return false;\n"
        "    }\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodTypedefAndNewtype) {
    const char* source =
        "typedef int32 Handle;\n"
        "newtype float Distance;\n";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, BadMissingSemicolon) {
    const char* source =
        "func bad :: () -> void {\n"
        "    let x :: int32 = 5\n"
        "    return;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_GT(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, BadMalformedIf) {
    const char* source =
        "func bad_if :: () -> int32 {\n"
        "    if (true) return 1;\n"
        "    return 0;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_GT(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodWhile) {
    const char* source =
        "func loop :: (n :: int32) -> int32 {\n"
        "    let i :: int32 = 0;\n"
        "    while (i < n) {\n"
        "        i = i + 1;\n"
        "    }\n"
        "    return i;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodFor) {
    const char* source =
        "func sum :: (n :: int32) -> int32 {\n"
        "    let s :: int32 = 0;\n"
        "    for (;;) {\n"
        "        s = s + 1;\n"
        "    }\n"
        "    return s;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodLambda) {
    const char* source =
        "lambda add :: (a :: int32, b :: int32) -> int32 {\n"
        "    return a + b;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodStructInitializer) {
    const char* source =
        "struct Vec { x :: float; y :: float; }\n"
        "func mk :: () -> Vec {\n"
        "    return Vec{ .x = 1.0, .y = 2.0 };\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodCollectionInitializer) {
    const char* source =
        "func nums :: () -> int32 {\n"
        "    let arr :: int32 = [int32: 5; 1, 2, 3, 4];\n"
        "    return 0;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodMemberAccessAndCall) {
    const char* source =
        "func foo :: (s :: int32) -> int32 {\n"
        "    let v :: int32 = s + 1;\n"
        "    return v;\n"
        "}\n"
        "func bar :: () -> int32 {\n"
        "    let x :: int32 = foo(42);\n"
        "    let obj :: int32 = 0;\n"
        "    let y :: int32 = obj;\n"
        "    return x + y;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodCastExpression) {
    const char* source =
        "func cast_test :: () -> int32 {\n"
        "    let f :: float = 3.14;\n"
        "    let i :: int32 = f as int32;\n"
        "    return i;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodArrayAccess) {
    const char* source =
        "func idx :: () -> int32 {\n"
        "    let arr :: int32 = [int32: 3; 10, 20, 30];\n"
        "    let v :: int32 = arr[1];\n"
        "    return v;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, BadMultipleErrorsRecovery) {
    const char* source =
        "func good1 :: () -> int32 {\n"
        "    return 1;\n"
        "}\n"
        "func bad_missing_brace :: () -> void {\n"
        "    let x :: int32 = 5\n" // missing semicolon
        "    return;\n"
        "}\n"
        "func good2 :: () -> int32 {\n"
        "    return 2;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_GT(parser_errors, 0);
}

TEST(ParserTest, TestLiteralNumerical) {
    const char* source = "func f ::() -> void { 42; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestLiteralBooleanTrue) {
    const char* source = "func f ::() -> void { true; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestBinaryAddition) {
    const char* source = "func f ::() -> void { 1 + 2; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestBinaryPrecedence) {
    const char* source = "func f ::() -> void { 1 + 2 * 3; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestParentheses) {
    const char* source = "func f ::() -> void { (1 + 2) * 3; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestUnaryMinusNumerical) {
    const char* source = "func f ::() -> void { -42; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestUnaryExclamationTrue) {
    const char* source = "func f ::() -> void { !true; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestFunctionCallNoArgs) {
    const char* source = "func f ::() -> void { foo(); }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestFunctionCallWithArgs) {
    const char* source = "func f ::() -> void { foo(1, true); }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestArrayListAccessIdentifierLiteralIndex) {
    const char* source = "func f ::() -> void { arr[0]; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestArrayListAccessNested) {
    const char* source = "func f ::() -> void { arr[0][1]; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestArrayListAccessWithBinaryIndex) {
    const char* source = "func f ::() -> void { arr[1 + 2 * 3]; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestParamDeclaration) {
    const char* source = "func f :: (foo :: int32) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestVariableDeclarationNoInit) {
    const char* source = "func f ::() -> void { let foo :: int32; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestVariableDeclarationWithInit) {
    const char* source = "func f ::() -> void { let foo :: int32 = 42; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypedefDeclSimple) {
    const char* source = "typedef int32 MyInt;";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestReturnWithExpression) {
    const char* source = "func foo ::() -> void { return 42; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestReturnWithoutExpression) {
    const char* source = "func foo ::() -> void { return; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestNewtypeDeclSimple) {
    const char* source = "newtype float MyFloat;";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestReturnWithExpressionBoolean) {
    const char* source = "func foo ::() -> void { return true; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestVariableDeclarationWithConstType) {
    const char* source = "func f ::() -> void { let foo :: const int32 = 0; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestVariableDeclarationWithRefType) {
    const char* source = "func f ::() -> void { let foo :: int32& = bar; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestBlockAssignmentWithComplexLhs) {
    const char* source = "func foo ::() -> void { x[2] = 1; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestFunctionCallNested) {
    const char* source = "func f ::() -> void { foo(bar(), 42); }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestArrayListAccessOnFunctionCall) {
    const char* source = "func f ::() -> void { foo()[0]; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestArrayListAccessFunctionCallOnAccess) {
    const char* source = "func f ::() -> void { arr[0](1); }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestBlockExpressionFunctionCallOnAccess) {
    const char* source = "func foo ::() -> void { x[1](); }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestBlockWithNestedBlock) {
    const char* source = "func foo ::() -> void { { x = 1; } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestBlockWithNestedBlockAndLet) {
    const char* source = "func foo ::() -> void { { let y :: bool; } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestReturnWithIdentifierExpression) {
    const char* source = "func foo ::() -> void { return x; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestLiteralBooleanFalse) {
    const char* source = "func f ::() -> void { false; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestIdentifier) {
    const char* source = "func f ::() -> void { foo; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestUnaryExclamationFalse) {
    const char* source = "func f ::() -> void { !false; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestUnaryNested) {
    const char* source = "func f ::() -> void { -!true; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestUnaryPrecedenceAdd) {
    const char* source = "func f ::() -> void { !a + b; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}
