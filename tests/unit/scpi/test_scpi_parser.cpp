/**
 * @file test_scpi_parser.cpp
 * @brief SCPI parser: header matching, compound messages, parameters, error queue, IEEE 488.2
 *        status registers.
 *
 * @verifies SRS-SCPI-001
 */
#include <gtest/gtest.h>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "satlink/scpi/parser.hpp"

namespace {

using satlink::scpi::Call;
using satlink::scpi::Error;
using satlink::scpi::Parser;

class ScpiParserTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        p.Add("MEASure:VOLTage[:DC]?", [](Call &c) { c.Reply(1.5); });
        p.Add("MEASure:CURRent?", [](Call &c) { c.Reply(2L); });
        p.Add("SOURce:LEVel", [this](Call &c) {
            if (const auto v = c.Number(0, -5.0, 5.0, 1.0, {"V", "MV"}); v && c.Expect(1, 1))
            {
                level = *v;
            }
        });
        p.Add("SOURce:LEVel?", [this](Call &c) { c.Reply(level); });
        p.Add("SOURce:STATe", [this](Call &c) {
            if (const auto b = c.Boolean(0); b && c.Expect(1, 1))
            {
                on = *b;
            }
        });
        p.Add("[SOURce]:MODE", [this](Call &c) {
            if (const auto m = c.Choice(0, {"FIXed", "SWEep", "list"}); m)
            {
                mode = *m;
            }
        });
        p.Add("SOURce:COUNt", [this](Call &c) {
            if (const auto n = c.Integer(0, 1, 10, 5); n)
            {
                count = *n;
            }
        });
        p.Add("TEXT", [this](Call &c) { text = c.Count() > 0 ? c.Raw(0) : ""; });
        p.Add("TEXT?", [](Call &c) {
            c.ReplyString("say \"hi\"");
            c.Reply(true);
        });
        p.Add("FAIL", [](Call &c) { c.Fail(Error::kExecution, "boom"); });
        p.Add("*IDN?", [](Call &c) { c.Reply(std::string_view("ACME,X,1,2")); });
    }

    std::string Err()
    {
        return p.Execute("SYST:ERR?");
    }

    Parser p;
    double level = 0.0;
    bool on = false;
    std::size_t mode = 99;
    long count = 0;
    std::string text;
};

TEST_F(ScpiParserTest, ShortAndLongFormsAreCaseInsensitive)
{
    EXPECT_EQ("1.5", p.Execute("MEAS:VOLT?"));
    EXPECT_EQ("1.5", p.Execute("measure:voltage?"));
    EXPECT_EQ("1.5", p.Execute(":Meas:Volt:DC?"));
    EXPECT_EQ("", p.Execute("MEASU:VOLT?")); // neither short nor long
    EXPECT_EQ("-113,\"Undefined header;MEASU:VOLT?\"", Err());
    EXPECT_EQ("0,\"No error\"", Err());
    EXPECT_EQ("ACME,X,1,2", p.Execute("*idn?"));
}

TEST_F(ScpiParserTest, CompoundMessagesKeepTheCurrentPath)
{
    EXPECT_EQ("1.5;2", p.Execute("MEAS:VOLT?;CURR?"));
    EXPECT_EQ("", p.Execute("SOUR:LEV 2.5;STAT ON"));
    EXPECT_DOUBLE_EQ(2.5, level);
    EXPECT_TRUE(on);
    // A common command does not change the path; ':' restarts at the root.
    EXPECT_EQ("ACME,X,1,2;2.5", p.Execute("SOUR:STAT OFF;*IDN?;LEV?"));
    EXPECT_EQ("1.5", p.Execute("SOUR:STAT 1;:MEAS:VOLT?"));
    EXPECT_EQ("", p.Execute(" ; ;"));
    EXPECT_EQ(0U, p.ErrorCount());
}

TEST_F(ScpiParserTest, OptionalNodesMayBeLeftOut)
{
    p.Execute("MODE SWE");
    EXPECT_EQ(1U, mode);
    p.Execute("SOUR:MODE fixed");
    EXPECT_EQ(0U, mode);
    p.Execute("MODE LIST");
    EXPECT_EQ(2U, mode);
    p.Execute("MODE SW");
    EXPECT_EQ("-224,\"Illegal parameter value;SW\"", Err());
}

TEST_F(ScpiParserTest, NumbersAcceptKeywordsAndUnits)
{
    p.Execute("SOUR:LEV MAX");
    EXPECT_DOUBLE_EQ(5.0, level);
    p.Execute("SOUR:LEV minimum");
    EXPECT_DOUBLE_EQ(-5.0, level);
    p.Execute("SOUR:LEV DEF");
    EXPECT_DOUBLE_EQ(1.0, level);
    p.Execute("SOUR:LEV -1.25E0 V");
    EXPECT_DOUBLE_EQ(-1.25, level);
    p.Execute("SOUR:LEV 3mv");
    EXPECT_DOUBLE_EQ(3.0, level); // units are accepted, not scaled
    EXPECT_EQ(0U, p.ErrorCount());

    p.Execute("SOUR:LEV 7");
    p.Execute("SOUR:LEV abc");
    p.Execute("SOUR:LEV 1 A");
    p.Execute("SOUR:LEV");
    p.Execute("SOUR:LEV 1,2");
    p.Execute("SOUR:LEV nan");
    EXPECT_EQ("-222,\"Data out of range;7\"", Err());
    EXPECT_EQ("-104,\"Data type error;abc\"", Err());
    EXPECT_EQ("-104,\"Data type error;1 A\"", Err());
    EXPECT_EQ("-109,\"Missing parameter\"", Err());
    EXPECT_EQ("-108,\"Parameter not allowed\"", Err());
    EXPECT_EQ("-104,\"Data type error;nan\"", Err());
    EXPECT_DOUBLE_EQ(3.0, level);
}

TEST_F(ScpiParserTest, IntegersAndBooleans)
{
    p.Execute("SOUR:COUN 7");
    EXPECT_EQ(7, count);
    p.Execute("SOUR:COUN DEF");
    EXPECT_EQ(5, count);
    p.Execute("SOUR:COUN 2.5");
    EXPECT_EQ("-104,\"Data type error;2.5\"", Err());
    p.Execute("SOUR:COUN");
    EXPECT_EQ("-109,\"Missing parameter\"", Err());
    p.Execute("SOUR:STAT maybe");
    EXPECT_EQ("-224,\"Illegal parameter value;maybe\"", Err());
    p.Execute("SOUR:STAT");
    EXPECT_EQ("-109,\"Missing parameter\"", Err());
    p.Execute("MODE");
    EXPECT_EQ("-109,\"Missing parameter\"", Err());
}

TEST_F(ScpiParserTest, QuotedStringsKeepSeparators)
{
    p.Execute("TEXT \"a;b,c\"");
    EXPECT_EQ("\"a;b,c\"", text);
    p.Execute("TEXT 'x'");
    EXPECT_EQ("'x'", text);
    EXPECT_EQ("\"say \"\"hi\"\"\",1", p.Execute("TEXT?"));
    const auto parts = satlink::scpi::SplitUnquoted("a,'b,c',\"d\"", ',');
    ASSERT_EQ(3U, parts.size());
    EXPECT_EQ("'b,c'", parts[1]);
}

TEST_F(ScpiParserTest, SyntaxErrors)
{
    p.Execute("MEAS::VOLT?");
    p.Execute("1MEAS?");
    p.Execute("*IDN:X?");
    p.Execute("SOUR:LEV 1,,2");
    p.Execute("?");
    p.Execute(std::string(Parser::kMaxLine + 1, 'A'));
    EXPECT_EQ(6U, p.ErrorCount());
    EXPECT_EQ("-102,\"Syntax error;MEAS::VOLT?\"", Err());
    EXPECT_EQ("-102,\"Syntax error;1MEAS?\"", Err());
    EXPECT_EQ("-102,\"Syntax error;*IDN:X?\"", Err());
    EXPECT_EQ("-102,\"Syntax error;SOUR:LEV 1,,2\"", Err());
    EXPECT_EQ("-102,\"Syntax error;?\"", Err());
    EXPECT_EQ("-100,\"Command error;program message too long\"", Err());
}

TEST_F(ScpiParserTest, FailedQueriesProduceNoResponse)
{
    p.Add("BAD?", [](Call &c) {
        c.Reply(1L);
        c.Fail(Error::kQueryError);
    });
    EXPECT_EQ("1.5", p.Execute("BAD?;MEAS:VOLT?"));
    EXPECT_EQ(satlink::scpi::kEsrQueryError, p.Esr());
    p.Execute("FAIL");
    EXPECT_EQ("-400,\"Query error\"", Err());
    EXPECT_EQ("-200,\"Execution error;boom\"", Err());
}

TEST_F(ScpiParserTest, ErrorQueueOverflowsIntoTheLastEntry)
{
    for (int i = 0; i < 20; ++i)
    {
        p.Execute("NOPE");
    }
    EXPECT_EQ("16", p.Execute("SYST:ERR:COUN?"));
    for (std::size_t i = 0; i < Parser::kQueueSize - 1; ++i)
    {
        EXPECT_EQ("-113,\"Undefined header;NOPE\"", p.Execute("SYST:ERR:NEXT?"));
    }
    EXPECT_EQ("-350,\"Queue overflow\"", Err());
    p.Execute("NOPE;NOPE");
    EXPECT_EQ("-113,\"Undefined header;NOPE\",-113,\"Undefined header;NOPE\"",
              p.Execute("SYST:ERR:ALL?"));
    EXPECT_EQ("0,\"No error\"", p.Execute("SYST:ERR:ALL?"));
}

TEST_F(ScpiParserTest, StatusRegisters)
{
    EXPECT_EQ("1999.0", p.Execute("SYST:VERS?"));
    EXPECT_EQ("1", p.Execute("*OPC?"));
    EXPECT_EQ("0", p.Execute("*ESR?"));
    p.Execute("*OPC;*WAI");
    EXPECT_EQ("1", p.Execute("*ESR?"));
    EXPECT_EQ("0", p.Execute("*ESR?")); // read clears

    p.Execute("*ESE 32;*SRE 32");
    EXPECT_EQ("32;32", p.Execute("*ESE?;*SRE?"));
    p.Execute("NOPE"); // command error
    p.Execute("FAIL"); // execution error
    EXPECT_EQ(0x04 | 0x20 | 0x40, p.Stb());
    EXPECT_EQ("100", p.Execute("*STB?"));
    p.Execute("*CLS");
    EXPECT_EQ(0U, p.ErrorCount());
    EXPECT_EQ("0", p.Execute("*STB?"));

    p.Execute("*ESE 256");
    p.Execute("*SRE");
    p.Execute("*CLS 1");
    EXPECT_EQ("-222,\"Data out of range;256\"", Err());
    EXPECT_EQ("-109,\"Missing parameter\"", Err());
    EXPECT_EQ("-108,\"Parameter not allowed\"", Err());
    EXPECT_EQ(0x20 | 0x10, p.Esr() & 0x30);
}

TEST(ScpiErrorTextTest, EveryErrorHasAText)
{
    for (const Error e : {Error::kNone, Error::kCommand, Error::kSyntax, Error::kDataType,
                          Error::kParameterNotAllowed, Error::kMissingParameter,
                          Error::kUndefinedHeader, Error::kExecution, Error::kSettingsConflict,
                          Error::kDataOutOfRange, Error::kIllegalParameterValue,
                          Error::kHardwareMissing, Error::kQueueOverflow, Error::kQueryError})
    {
        EXPECT_NE("Unknown error", satlink::scpi::ErrorText(e));
    }
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): the point of the check
    EXPECT_EQ("Unknown error", satlink::scpi::ErrorText(static_cast<Error>(-999)));
}

TEST(ScpiCallTest, NumbersAreFormattedCompactly)
{
    const std::vector<std::string> none;
    Call c(none, true);
    c.Reply(0.000125);
    c.Reply(12.5);
    c.Reply(std::numeric_limits<double>::quiet_NaN());
    c.Reply(std::numeric_limits<double>::infinity());
    c.Reply(-std::numeric_limits<double>::infinity());
    c.Reply(-3L);
    c.Reply(false);
    EXPECT_EQ("0.000125,12.5,9.91E+37,9.9E+37,-9.9E+37,-3,0", c.Response());
    EXPECT_TRUE(c.IsQuery());
    std::optional<std::size_t> choice = c.Choice(0, {"A"});
    EXPECT_FALSE(choice.has_value());
    EXPECT_FALSE(c.Number(0, 0, 1).has_value());
    EXPECT_EQ(2U, c.Errors().size());
}

} // namespace
