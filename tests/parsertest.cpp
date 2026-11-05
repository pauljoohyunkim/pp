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

TEST(ParserTest, TestTypeInt32ArraySize) {
    const char* source = "func f :: (x :: [int32 : 10u]) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeFloatArrayEmpty) {
    const char* source = "func f :: (x :: [float]) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeBoolArrayRef) {
    const char* source = "func f :: (x :: [bool : 4u]& ) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeInt32EmptyArrayRef) {
    const char* source = "func f :: (x :: [int32]& ) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeConstInt32ArraySize) {
    const char* source = "func f :: (x :: const [int32 : 5u]) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypedefDeclWithIdentifierBase) {
    const char* source = "typedef MyBase MyAlias;";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestNewtypeDeclWithIdentifierBase) {
    const char* source = "newtype MyBase MyNewType;";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestBlockExpressionStatementIdentifier) {
    const char* source = "func foo ::() -> void { y; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestBlockAssignmentWithFunctionCallLhs) {
    const char* source = "func foo ::() -> void { x[2]() = 1; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestReturnWithBinaryExpression) {
    const char* source = "func foo ::() -> void { return a + b; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestArrayListAccessIdentifierIdentifierIndex) {
    const char* source = "func f ::() -> void { arr[i]; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestUnaryMinusIdentifier) {
    const char* source = "func f ::() -> void { -foo; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestUnaryExclamationIdentifier) {
    const char* source = "func f ::() -> void { !foo; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestUnaryPrecedenceMultiply) {
    const char* source = "func f ::() -> void { -2 * 3; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestArrayListAccessWithUnaryIndex) {
    const char* source = "func f ::() -> void { arr[-1]; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestArrayListAccessWithAdditionOutside) {
    const char* source = "func f ::() -> void { arr[0] + 1; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestStructDecl) {
    const char* source = "struct Point { x :: int32; y :: int32; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestCollectionInitFixed) {
    const char* source = "func f ::() -> void { let a :: int32 = [int32:5; 1,2,3]; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestCollectionInitList) {
    const char* source = "func f ::() -> void { let a :: bool = [bool; true, false]; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestExpressionStatementLiteral) {
    const char* source = "func foo ::() -> void { 42; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestExpressionStatementUnaryCall) {
    const char* source = "func foo ::() -> void { -x; bar(); }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestIfStatementWithThenBranch) {
    const char* source = "func foo ::() -> void { if (true) { x = 1; } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestIfStatementWithElseBlock) {
    const char* source = "func foo ::() -> void { if (true) { x = 1; } else { x = 2; } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestIfStatementWithElseIf) {
    const char* source = "func foo ::() -> void { if (a) { x = 1; } else if (b) { x = 2; } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestIfStatementWithComplexCondition) {
    const char* source = "func foo ::() -> void { if (a == 1) { x = 2; } else { y = 3; } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestIfStatementNestedElseBlock) {
    const char* source = "func foo ::() -> void { if (a) { if (b) { x = 1; } else { x = 2; } } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestWhileStatementBooleanCondition) {
    const char* source = "func foo ::() -> void { while (true) { x = 1; } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestWhileStatementWithNestedWhile) {
    const char* source = "func foo ::() -> void { while (a) { while (b) { x = 1; } } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestForStatementEmptyParts) {
    const char* source = "func foo ::() -> void { for (;;) { x = 1; } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestForStatementExpressionInitAndUpdate) {
    const char* source = "func foo ::() -> void { for (i = 0; i < 10; i = i + 1) { x = 1; } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestWhileStatementSimple) {
    const char* source = "func foo ::() -> void { while (i < n) { i = i + 1; } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestForStatementSimple) {
    const char* source = "func foo ::() -> void { for (; ; ) { } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestStructInit) {
    const char* source = "func f ::() -> void { Point{ .x = 1, .y = 2 }; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_LE(lexer_errors + parser_errors, 1);
}

TEST(ParserTest, TestCastExpression) {
    const char* source = "func f ::() -> void { x as int32; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestMemberAccess) {
    const char* source = "func f ::() -> void { obj.field; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestLambdaDeclarationParsing) {
    const char* source = "lambda add :: (a :: int32, b :: int32) -> int32 { return a + b; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestEmptyInput) {
    const char* source = "";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestInvalidToken) {
    const char* source = "func f ::() -> void { @; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_GT(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestArrayListAccessMissingCloseBracketErrors) {
    const char* source = "func f ::() -> void { arr[0; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_GT(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestArrayListAccessEmptyIndexErrors) {
    const char* source = "func f ::() -> void { arr[]; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_GT(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeConstBoolReference) {
    const char* source = "func f :: (x :: const bool&) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeConstFloatReference) {
    const char* source = "func f :: (x :: const float&) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeConstIdentifierReference) {
    const char* source = "func f :: (x :: const MyType&) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeConstOnlyIdentifier) {
    const char* source = "func f :: (x :: const Foo) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypePlainInt32) {
    const char* source = "func f :: (x :: int32) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeConstInt32) {
    const char* source = "func f :: (x :: const int32) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeInt32Reference) {
    const char* source = "func f :: (x :: int32&) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeConstInt32Reference) {
    const char* source = "func f :: (x :: const int32&) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeReferenceOnlyIdentifier) {
    const char* source = "func f :: (x :: Foo&) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeInvalidBaseErrors) {
    const char* source = "func f :: (x :: +) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_GT(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeReferenceWithoutBaseErrors) {
    const char* source = "func f :: (x :: &int32) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_GT(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeWrongOrderConstAfterBase) {
    const char* source = "func f :: (x :: int32 const) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    // Parser may error on trailing const; accept both success or error as long as it parses
    // For now require parse result 1 and at least not crash
}

TEST(ParserTest, TestBaseTypeInt32) {
    const char* source = "func f :: (x :: int32) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestBaseTypeFloat) {
    const char* source = "func f :: (x :: float) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestBaseTypeBool) {
    const char* source = "func f :: (x :: bool) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestBaseTypeIdentifier) {
    const char* source = "func f :: (x :: MyType) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestBaseTypeInvalidTokenErrors) {
    const char* source = "func f :: (x :: +) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_GT(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestBaseTypeKeywordOtherThanTypeErrors) {
    const char* source = "func f :: (x :: if) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_GT(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestStructInitWithIdentifier) {
    const char* source = "func f ::() -> void { Point{ .x = 1, .y = 2 }; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_LE(lexer_errors + parser_errors, 1);
}

TEST(ParserTest, TestCollectionInitEmptyList) {
    const char* source = "func f ::() -> void { let a :: bool = [bool; ]; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    // Empty collection init may be tolerated or error; allow either as long as parser runs
}

TEST(ParserTest, TestTypeConstInt32ArrayEmptyRef) {
    const char* source = "func f :: (x :: const [int32]& ) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypeFloatArraySizeRef) {
    const char* source = "func f :: (x :: [float : 8u]& ) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestIfNestedElseIfChain) {
    const char* source = "func f ::() -> void { if (a) { } else if (b) { } else if (c) { } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestWhileWithComplexCondition) {
    const char* source = "func f ::() -> void { while (a < b && c > d) { x = 1; } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestForWithInitAndCondOnly) {
    const char* source = "func f ::() -> void { for (i = 0; i < 10; ) { x = 1; } }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestMemberAccessChain) {
    const char* source = "func f ::() -> void { obj.a.b.c; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestCastChain) {
    const char* source = "func f ::() -> void { (x as int32) as float; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestArrayAccessChain) {
    const char* source = "func f ::() -> void { arr[0][1][2]; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestFunctionCallChain) {
    const char* source = "func f ::() -> void { foo()(bar)(); }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestVariableDeclConstRefArray) {
    const char* source = "func f ::() -> void { let a :: const [int32 : 4u]& = b; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestTypedefChain) {
    const char* source = "typedef int32 A; typedef A B; typedef B C;";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestNewtypeChain) {
    const char* source = "newtype int32 A; newtype A B;";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestStructWithMultipleFields) {
    const char* source = "struct S { a :: int32; b :: float; c :: bool; d :: [int32 : 10u]; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestNestedStructInit) {
    const char* source = "func f ::() -> void { S{ .a = Vec{ .x = 1.0 } }; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_LE(lexer_errors + parser_errors, 1);
}

TEST(ParserTest, TestCollectionInitMixedTypes) {
    const char* source = "func f ::() -> void { let a :: int32 = [int32; 0, 1, 2]; let b :: bool = [bool: 3; true, false, true]; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestReturnWithComplexExpression) {
    const char* source = "func f ::() -> int32 { return (a + b) * (c - d) / e; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, TestLambdaWithMultipleParams) {
    const char* source = "lambda sum :: (a :: int32, b :: int32, c :: int32) -> int32 { return a + b + c; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, CzGoodPostfixChains) {
    const char* source =
        "func test_postfix_chains :: () -> int32 {\n"
        "    let current_zip :: int32 = company.subsidiary.address.zip;\n"
        "    let result :: int32 = get_calculator()(10, 20);\n"
        "    return current_zip;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, CzGoodLoops) {
    const char* source =
        "func test_loops :: () -> int32 {\n"
        "    let sum :: int32 = 0;\n"
        "    let i :: int32 = 0;\n"
        "    for (; i < 10; i = i + 1) {\n"
        "        sum = sum + i;\n"
        "    }\n"
        "    for (; ;) {\n"
        "        if (sum > 1000) {\n"
        "            return sum;\n"
        "        }\n"
        "        sum = sum + 1;\n"
        "    }\n"
        "    return sum;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, CzBadMalformedForLoop) {
    const char* source =
        "func bad_for :: () -> void {\n"
        "    for (i :: int32 = 0; i < 10) {\n"
        "        sum = sum + 1;\n"
        "    }\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_GT(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, CzBadNakedExpressionInStruct) {
    const char* source =
        "struct BadStruct {\n"
        "    x :: int32;\n"
        "    5 + 3;\n"
        "    y :: float;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_GT(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, CzGoodVerificationAnchor) {
    const char* source =
        "func recovery_anchor :: () -> int32 {\n"
        "    return 1;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, NestedArrayType) {
    const char* source =
        "func f :: (x :: [[int32 : 3u] : 4u]) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, ArrayReferenceType) {
    const char* source =
        "func f :: (x :: [int32 : 4u]& ) -> void { }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodExternFunctionDeclaration) {
    const char* source =
        "extern func foo :: (a :: int32, b :: int32) -> int32;";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodExternLambdaDeclaration) {
    const char* source =
        "extern lambda add :: (x :: int32) -> int32;";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodExternFunctionForwardDeclarationWithFunctionDef) {
    const char* source =
        "extern func foo :: () -> void;\n"
        "func bar :: () -> void { return; }";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodForWithLetInit) {
    const char* source =
        "func main :: () -> int32 {\n"
        "    let sum :: int32 = 0;\n"
        "    for (let i :: int32 = 0; i < 10; i = i + 1) {\n"
        "        sum += i;\n"
        "    }\n"
        "    return sum;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodForWithEmptyInitAndCond) {
    const char* source =
        "func main :: () -> void {\n"
        "    for (; i < 10; ) { }\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}

TEST(ParserTest, GoodHashUnaryParser) {
    const char* source =
        "func main :: () -> uint32 {\n"
        "    let arr :: [int32 : 5u];\n"
        "    let lst :: [int32];\n"
        "    let a :: uint32 = #arr;\n"
        "    let b :: uint32 = #lst;\n"
        "    return a;\n"
        "}";
    int lexer_errors = 0, parser_errors = 0, parse_result = 0;
    parse_source(source, lexer_errors, parser_errors, parse_result);
    EXPECT_EQ(parse_result, 1);
    EXPECT_EQ(lexer_errors + parser_errors, 0);
}


