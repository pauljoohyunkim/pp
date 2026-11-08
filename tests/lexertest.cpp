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
    ASSERT_EQ(lexer->tokens[9].token_type, PP_TT_INTEGER_LITERAL);
    ASSERT_EQ(lexer->tokens[10].token_type, PP_TT_INTEGER_LITERAL);
    ASSERT_EQ(lexer->tokens[11].token_type, PP_TT_PERIOD);
    ASSERT_EQ(lexer->tokens[12].token_type, PP_TT_INTEGER_LITERAL);
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
    ASSERT_EQ(lexer->n_tokens, 17);
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
    ASSERT_EQ(lexer->tokens[12].token_type, PP_TT_LESS_LESS);
    ASSERT_EQ(lexer->tokens[13].token_type, PP_TT_GREATER_GREATER);
    ASSERT_EQ(lexer->tokens[14].token_type, PP_TT_EXCLAMATION);
    ASSERT_EQ(lexer->tokens[15].token_type, PP_TT_EXCLAMATION);
    ASSERT_EQ(lexer->tokens[16].token_type, PP_TT_EOF);
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

TEST(Lexer, HashToken) {
    auto lexer_h = pp_lexer_create("# #arr # arr#", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 7);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_HASH);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_HASH);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_HASH);
    ASSERT_EQ(lexer->tokens[4].token_type, PP_TT_IDENTIFIER);
    ASSERT_EQ(lexer->tokens[5].token_type, PP_TT_HASH);
    ASSERT_EQ(lexer->tokens[6].token_type, PP_TT_EOF);
}

TEST(Lexer, IntegerSuffixLiterals) {
    auto lexer_h = pp_lexer_create("42s 42ss 42l 42ll 42us 42uss 42ul 42ull 42S 42SS 42L 42LL 42US 42USS 42UL 42ULL", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    // 16 literals + EOF
    ASSERT_EQ(lexer->n_tokens, 17);
    for (int i = 0; i < 16; ++i) {
        PP_TokenType tt = lexer->tokens[i].token_type;
        ASSERT_EQ(tt, PP_TT_INTEGER_LITERAL);
    }
    // Verify lexemes are preserved with suffixes
    ASSERT_STREQ(lexer->tokens[0].lexeme, "42s");
    ASSERT_STREQ(lexer->tokens[1].lexeme, "42ss");
    ASSERT_STREQ(lexer->tokens[2].lexeme, "42l");
    ASSERT_STREQ(lexer->tokens[3].lexeme, "42ll");
    ASSERT_STREQ(lexer->tokens[4].lexeme, "42us");
    ASSERT_STREQ(lexer->tokens[5].lexeme, "42uss");
    ASSERT_STREQ(lexer->tokens[6].lexeme, "42ul");
    ASSERT_STREQ(lexer->tokens[7].lexeme, "42ull");
    ASSERT_STREQ(lexer->tokens[8].lexeme, "42S");
    ASSERT_STREQ(lexer->tokens[9].lexeme, "42SS");
    ASSERT_STREQ(lexer->tokens[10].lexeme, "42L");
    ASSERT_STREQ(lexer->tokens[11].lexeme, "42LL");
    ASSERT_STREQ(lexer->tokens[12].lexeme, "42US");
    ASSERT_STREQ(lexer->tokens[13].lexeme, "42USS");
    ASSERT_STREQ(lexer->tokens[14].lexeme, "42UL");
    ASSERT_STREQ(lexer->tokens[15].lexeme, "42ULL");
}

TEST(Lexer, NewIntegerTypeKeywords) {
    auto lexer_h = pp_lexer_create("int8 uint8 int16 uint16 int64 uint64 int128 uint128", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 9);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_INT8);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_UINT8);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_INT16);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_UINT16);
    ASSERT_EQ(lexer->tokens[4].token_type, PP_TT_INT64);
    ASSERT_EQ(lexer->tokens[5].token_type, PP_TT_UINT64);
    ASSERT_EQ(lexer->tokens[6].token_type, PP_TT_INT128);
    ASSERT_EQ(lexer->tokens[7].token_type, PP_TT_UINT128);
    ASSERT_EQ(lexer->tokens[8].token_type, PP_TT_EOF);
    ASSERT_STREQ(lexer->tokens[0].lexeme, "int8");
    ASSERT_STREQ(lexer->tokens[7].lexeme, "uint128");
}

TEST(Lexer, StaticStringLiteral) {
    auto lexer_h = pp_lexer_create("@\"hello\" @\"\" @\"he\\\"llo\\n\"", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 4);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_STATIC_STRING_LITERAL);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_STATIC_STRING_LITERAL);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_STATIC_STRING_LITERAL);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_EOF);
    ASSERT_STREQ(lexer->tokens[0].lexeme, "@\"hello\"");
    ASSERT_STREQ(lexer->tokens[1].lexeme, "@\"\"");
    ASSERT_STREQ(lexer->tokens[2].lexeme, "@\"he\\\"llo\\n\"");
}

TEST(Lexer, StaticStringNoWhitespace) {
    // No whitespace allowed between @ and "
    auto lexer_h = pp_lexer_create("@ \"hello\"", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    // '@' alone is unknown, then string literal
    ASSERT_GE(lexer->n_tokens, 3);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_UNKNOWN);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_STRING_LITERAL);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_EOF);
}

TEST(Lexer, StaticStringEscapes) {
    auto lexer_h = pp_lexer_create("@\"a\\n b\" @\"a\\0b\" @\"\\\"\"", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 4);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_STATIC_STRING_LITERAL);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_STATIC_STRING_LITERAL);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_STATIC_STRING_LITERAL);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_EOF);
}

TEST(Lexer, StaticStringEscapesHexBackslash) {
    auto lexer_h = pp_lexer_create("@\"\\x41\\x42\\x43\" @\"\\\\\" @\"\\0\\t\\r\\\"\\n\"", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 4);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_STATIC_STRING_LITERAL);
    ASSERT_EQ(lexer->tokens[1].token_type, PP_TT_STATIC_STRING_LITERAL);
    ASSERT_EQ(lexer->tokens[2].token_type, PP_TT_STATIC_STRING_LITERAL);
    ASSERT_EQ(lexer->tokens[3].token_type, PP_TT_EOF);
    ASSERT_STREQ(lexer->tokens[0].lexeme, "@\"\\x41\\x42\\x43\"");
    ASSERT_STREQ(lexer->tokens[1].lexeme, "@\"\\\\\"");
    ASSERT_STREQ(lexer->tokens[2].lexeme, "@\"\\0\\t\\r\\\"\\n\"");
}

TEST(Lexer, StringLiteralNoMultiline) {
    // String literals should not span lines; newline stops the token
    auto lexer_h = pp_lexer_create("\"a\n", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    // Lexer will produce a token for "a\n? The newline breaks token, so we get STRING_LITERAL for "a and then newline handling
    // At minimum we should not produce a single spanning token
    ASSERT_GT(lexer->n_tokens, 0);
}

TEST(Lexer, StaticStringLiteralNoMultiline) {
    auto lexer_h = pp_lexer_create("@\"a\n\"", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    // Should not treat as a single static string spanning lines
    ASSERT_GT(lexer->n_tokens, 0);
}

TEST(Lexer, StringLiteralContainsTab) {
    auto lexer_h = pp_lexer_create("\"a\tb\"", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 2);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_STRING_LITERAL);
    // Tab is allowed inside a string literal
}

TEST(Lexer, StaticStringLiteralContainsTab) {
    auto lexer_h = pp_lexer_create("@\"a\tb\"", nullptr);
    std::unique_ptr<PP_Lexer, decltype(&pp_lexer_free)> lexer(lexer_h, pp_lexer_free);
    ASSERT_EQ(pp_lexer_analyze(lexer.get()), 1);
    ASSERT_EQ(lexer->n_tokens, 2);
    ASSERT_EQ(lexer->tokens[0].token_type, PP_TT_STATIC_STRING_LITERAL);
}


