#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <cstring>
#include "pp_lexer.h"
#include "pp_tokens.h"

TEST(Lexer, AnalysisLength) {
    auto lexer_h = pp_lexer_create("( { ( [ (  \n )]))}", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 11);

    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_LEFT_PARENTHESIS);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_LEFT_CURLY_BRACKET);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_LEFT_PARENTHESIS);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_LEFT_SQUARE_BRACKET);
    ASSERT_EQ(lexer->tokens[4].token_type, PP_TT_LEFT_PARENTHESIS);
    ASSERT_EQ(lexer->tokens[5].token_type, PP_TT_RIGHT_PARENTHESIS);
    ASSERT_EQ(lexer->tokens[6].token_type, PP_TT_RIGHT_SQUARE_BRACKET);
    ASSERT_EQ(lexer->tokens[7].token_type, PP_TT_RIGHT_PARENTHESIS);
    ASSERT_EQ(lexer->tokens[8].token_type, PP_TT_RIGHT_PARENTHESIS);
    ASSERT_EQ(lexer->tokens[9].token_type, PP_TT_RIGHT_CURLY_BRACKET);
    ASSERT_EQ(lexer->tokens[10].token_type, PP_TT_EOF);
}

TEST(Lexer, AnalysisComment) {
    auto lexer_h = pp_lexer_create("- == // %@13 +- == \n//\n->", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 4);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_MINUS);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_EQUAL_EQUAL);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_RIGHT_ARROW);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_EOF);
}

TEST(Lexer, AnalysisEqual) {
    auto lexer_h = pp_lexer_create("= ==", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 3);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_EQUAL);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_EQUAL_EQUAL);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_EOF);
}

TEST(Lexer, Keywords) {
    auto lexer_h = pp_lexer_create("return func int32", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 4);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_RETURN);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_FUNC);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_INT32);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_EOF);
}

TEST(Lexer, Identifiers) {
    auto lexer_h = pp_lexer_create("return1 inv func30j2", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 4);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_EOF);
    ASSERT_STREQ(lexer->tokens[0].lexeme, "return1");
    ASSERT_STREQ(lexer->tokens[1].lexeme, "inv");
    ASSERT_STREQ(lexer->tokens[2].lexeme, "func30j2");
}

TEST(Lexer, NumericLiterals) {
    auto lexer_h = pp_lexer_create("0 1.0 3.", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 4);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_INTEGER_LITERAL);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_FLOAT_LITERAL);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_FLOAT_LITERAL);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_EOF);
}

TEST(Lexer, Unknown) {
    auto lexer_h = pp_lexer_create("a bd ~ `", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    // a identifier, bd identifier, ~ unknown, ` unknown, EOF
    ASSERT_GE(lexer->n_tokens, 5);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_UNKNOWN);
    // error list should have at least one error for unknown
    ASSERT_GT(lexer->error_list->n_errors, 0);
}

TEST(Lexer, SingleCharacterTokens) {
    auto lexer_h = pp_lexer_create("()+-*/;.,{}[]", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 14);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_LEFT_PARENTHESIS);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_RIGHT_PARENTHESIS);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_PLUS);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_MINUS);
    ASSERT_EQ(lexer->tokens[4].token_type, PP_TT_STAR);
    ASSERT_EQ(lexer->tokens[5].token_type, PP_TT_SLASH);
    ASSERT_EQ(lexer->tokens[6].token_type, PP_TT_SEMICOLON);
    ASSERT_EQ(lexer->tokens[7].token_type, PP_TT_PERIOD);
    ASSERT_EQ(lexer->tokens[8].token_type, PP_TT_COMMA);
    ASSERT_EQ(lexer->tokens[9].token_type, PP_TT_LEFT_CURLY_BRACKET);
    ASSERT_EQ(lexer->tokens[10].token_type, PP_TT_RIGHT_CURLY_BRACKET);
    ASSERT_EQ(lexer->tokens[11].token_type, PP_TT_LEFT_SQUARE_BRACKET);
    ASSERT_EQ(lexer->tokens[12].token_type, PP_TT_RIGHT_SQUARE_BRACKET);
    ASSERT_EQ(lexer->tokens[13].token_type, PP_TT_EOF);
}

TEST(Lexer, DoubleCharacterTokens) {
    auto lexer_h = pp_lexer_create("== += -= *= /= %= &= |= ^= == != >= <= => :: && ||", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 19);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_EQUAL_EQUAL);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_PLUS_EQUAL);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_MINUS_EQUAL);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_STAR_EQUAL);
    ASSERT_EQ(lexer->tokens[4].token_type, PP_TT_SLASH_EQUAL);
    ASSERT_EQ(lexer->tokens[5].token_type, PP_TT_PERCENT_EQUAL);
    ASSERT_EQ(lexer->tokens[6].token_type, PP_TT_AMPERSAND_EQUAL);
    ASSERT_EQ(lexer->tokens[7].token_type, PP_TT_BAR_EQUAL);
    ASSERT_EQ(lexer->tokens[8].token_type, PP_TT_CARET_EQUAL);
    ASSERT_EQ(lexer->tokens[9].token_type, PP_TT_EQUAL_EQUAL);
    ASSERT_EQ(lexer->tokens[10].token_type, PP_TT_EXCLAMATION_EQUAL);
    ASSERT_EQ(lexer->tokens[11].token_type, PP_TT_GREATER_EQUAL);
    ASSERT_EQ(lexer->tokens[12].token_type, PP_TT_LESS_EQUAL);
    ASSERT_EQ(lexer->tokens[13].token_type, PP_TT_EQUAL);
    ASSERT_EQ(lexer->tokens[14].token_type, PP_TT_GREATER);
    ASSERT_EQ(lexer->tokens[15].token_type, PP_TT_COLON_COLON);
    ASSERT_EQ(lexer->tokens[16].token_type, PP_TT_AMPERSAND_AMPERSAND);
    ASSERT_EQ(lexer->tokens[17].token_type, PP_TT_BAR_BAR);
    ASSERT_EQ(lexer->tokens[18].token_type, PP_TT_EOF);
}

TEST(Lexer, KeywordBoundaries) {
    auto lexer_h = pp_lexer_create("if ifif else elseif endif for foreach format", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 9);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_IF);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_ELSE);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[4].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[5].token_type, PP_TT_FOR);
    ASSERT_EQ(lexer->tokens[6].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[7].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[8].token_type, PP_TT_EOF);
    ASSERT_STREQ(lexer->tokens[0].lexeme, "if");
    ASSERT_STREQ(lexer->tokens[1].lexeme, "ifif");
    ASSERT_STREQ(lexer->tokens[2].lexeme, "else");
}

TEST(Lexer, LineAndColumnTracking) {
    auto lexer_h = pp_lexer_create("a\nb", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 3);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_EOF);
    ASSERT_STREQ(lexer->tokens[0].lexeme, "a");
    ASSERT_EQ(lexer->tokens[0].line, 1);
    ASSERT_EQ(lexer->tokens[0].column, 1);
    ASSERT_STREQ(lexer->tokens[1].lexeme, "b");
    ASSERT_EQ(lexer->tokens[1].line, 2);
    ASSERT_EQ(lexer->tokens[1].column, 1);
}

TEST(Lexer, WhitespaceSkipping) {
    auto lexer_h = pp_lexer_create("   int32\t\n\x0b\x0c\r   foo   ", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 3);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_INT32);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_EOF);
    ASSERT_STREQ(lexer->tokens[1].lexeme, "foo");
}

TEST(Lexer, EOFPositionTracking) {
    auto lexer_h = pp_lexer_create("hello\nworld", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 3);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_EOF);
    ASSERT_STREQ(lexer->tokens[0].lexeme, "hello");
    ASSERT_EQ(lexer->tokens[0].line, 1);
    ASSERT_EQ(lexer->tokens[0].column, 1);
    ASSERT_STREQ(lexer->tokens[1].lexeme, "world");
    ASSERT_EQ(lexer->tokens[1].line, 2);
    ASSERT_EQ(lexer->tokens[1].column, 1);
    ASSERT_EQ(lexer->tokens[2].line, 2);
}

TEST(Lexer, NumericEdgeCases) {
    auto lexer_h = pp_lexer_create(".5 5. 5.5.5 5..5 5u5U .5u 5.u", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 16);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_PERIOD);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_INTEGER_LITERAL);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_FLOAT_LITERAL);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_FLOAT_LITERAL);
    ASSERT_EQ(lexer->tokens[4].token_type, PP_TT_PERIOD);
    ASSERT_EQ(lexer->tokens[5].token_type, PP_TT_INTEGER_LITERAL);
    ASSERT_EQ(lexer->tokens[6].token_type, PP_TT_FLOAT_LITERAL);
    ASSERT_EQ(lexer->tokens[7].token_type, PP_TT_PERIOD);
    ASSERT_EQ(lexer->tokens[8].token_type, PP_TT_INTEGER_LITERAL);
    ASSERT_EQ(lexer->tokens[9].token_type, PP_TT_UNSIGNED_INTEGER_LITERAL);
    ASSERT_EQ(lexer->tokens[10].token_type, PP_TT_UNSIGNED_INTEGER_LITERAL);
    ASSERT_EQ(lexer->tokens[11].token_type, PP_TT_PERIOD);
    ASSERT_EQ(lexer->tokens[12].token_type, PP_TT_UNSIGNED_INTEGER_LITERAL);
    ASSERT_EQ(lexer->tokens[13].token_type, PP_TT_FLOAT_LITERAL);
    ASSERT_EQ(lexer->tokens[14].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[15].token_type, PP_TT_EOF);
    ASSERT_STREQ(lexer->tokens[0].lexeme, ".");
    ASSERT_STREQ(lexer->tokens[1].lexeme, "5");
    ASSERT_STREQ(lexer->tokens[2].lexeme, "5.");
}

TEST(Lexer, IdentifierEdgeCases) {
    auto lexer_h = pp_lexer_create("_ _test test_ test123 123test __ __test__ TEST test TestTest", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 12);
    // C-style lexer allows identifiers starting with underscore
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_IDENTIFIER);
    ASSERT_STREQ(lexer->tokens[0].lexeme, "_");
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_IDENTIFIER);
    ASSERT_STREQ(lexer->tokens[1].lexeme, "_test");
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[4].token_type, PP_TT_INTEGER_LITERAL);
    ASSERT_EQ(lexer->tokens[5].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[6].token_type, PP_TT_IDENTIFIER);
    ASSERT_STREQ(lexer->tokens[6].lexeme, "__");
    ASSERT_EQ(lexer->tokens[7].token_type, PP_TT_IDENTIFIER);
    ASSERT_STREQ(lexer->tokens[7].lexeme, "__test__");
    ASSERT_EQ(lexer->tokens[8].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[9].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[10].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[11].token_type, PP_TT_EOF);
}

TEST(Lexer, OperatorCombinations) {
    auto lexer_h = pp_lexer_create("++ -- ** &&|| ==!= <=>= <<>> !!", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 19);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_PLUS);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_PLUS);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_MINUS);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_MINUS);
    ASSERT_EQ(lexer->tokens[4].token_type, PP_TT_STAR);
    ASSERT_EQ(lexer->tokens[5].token_type, PP_TT_STAR);
    ASSERT_EQ(lexer->tokens[6].token_type, PP_TT_AMPERSAND_AMPERSAND);
    ASSERT_EQ(lexer->tokens[7].token_type, PP_TT_BAR_BAR);
    ASSERT_EQ(lexer->tokens[8].token_type, PP_TT_EQUAL_EQUAL);
    ASSERT_EQ(lexer->tokens[9].token_type, PP_TT_EXCLAMATION_EQUAL);
    ASSERT_EQ(lexer->tokens[10].token_type, PP_TT_LESS_EQUAL);
    ASSERT_EQ(lexer->tokens[11].token_type, PP_TT_GREATER_EQUAL);
    ASSERT_EQ(lexer->tokens[12].token_type, PP_TT_LESS);
    ASSERT_EQ(lexer->tokens[13].token_type, PP_TT_LESS);
    ASSERT_EQ(lexer->tokens[14].token_type, PP_TT_GREATER);
    ASSERT_EQ(lexer->tokens[15].token_type, PP_TT_GREATER);
    ASSERT_EQ(lexer->tokens[16].token_type, PP_TT_EXCLAMATION);
    ASSERT_EQ(lexer->tokens[17].token_type, PP_TT_EXCLAMATION);
    ASSERT_EQ(lexer->tokens[18].token_type, PP_TT_EOF);
}

TEST(Lexer, MixedWhitespaceAndComments) {
    auto lexer_h = pp_lexer_create("  int32 // this is a comment\n\t  x = 5 /* not a block comment */ ;  \r\n", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 14);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_INT32);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_EQUAL);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_INTEGER_LITERAL);
    ASSERT_EQ(lexer->tokens[4].token_type, PP_TT_SLASH);
    ASSERT_EQ(lexer->tokens[5].token_type, PP_TT_STAR);
    ASSERT_EQ(lexer->tokens[6].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[7].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[8].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[9].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[10].token_type, PP_TT_STAR);
    ASSERT_EQ(lexer->tokens[11].token_type, PP_TT_SLASH);
    ASSERT_EQ(lexer->tokens[12].token_type, PP_TT_SEMICOLON);
    ASSERT_EQ(lexer->tokens[13].token_type, PP_TT_EOF);
}

TEST(Lexer, StringLiteralBehavior) {
    auto lexer_h = pp_lexer_create("\"\" \"a\" \"hello world\" \"123\" \"_test\"", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 6);
    for (int i = 0; i < 5; ++i) {
        ASSERT_EQ(lexer->tokens[i].token_type, PP_TT_STRING_LITERAL);
    }
    ASSERT_EQ(lexer->tokens[5].token_type, PP_TT_EOF);
}
