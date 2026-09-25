/**
 * @file test_scpi_instrument.cpp
 * @brief The payload's SCPI command tree against the payload manager and the simulated modem.
 *
 * @verifies SRS-SCPI-002
 */
#include <algorithm>
#include <cstdlib>
#include <gtest/gtest.h>
#include <string>

#include "satlink/payload/scpi_instrument.hpp"

#include "payload_harness.hpp"

namespace satlink::test {
namespace {

std::size_t Elements(const std::string &response)
{
    return static_cast<std::size_t>(std::count(response.begin(), response.end(), ',')) + 1U;
}

class ScpiInstrumentTest : public ::testing::Test
{
  protected:
    std::string Q(std::string_view line)
    {
        return scpi.Execute(line);
    }
    double D(std::string_view line)
    {
        return std::strtod(Q(line).c_str(), nullptr);
    }
    std::string Err()
    {
        return Q("SYST:ERR?");
    }

    PayloadHarness h;
    payload::ScpiInstrument scpi{h.manager, "SN42", "1.0.0"};
};

TEST_F(ScpiInstrumentTest, IdentityAndSelfTest)
{
    EXPECT_EQ("SatLink-Z7,Payload Modem,SN42,1.0.0", Q("*IDN?"));
    EXPECT_EQ("1", Q("*TST?")); // no status from the modem yet
    h.Run(1500);
    EXPECT_EQ("0", Q("*TST?"));
    EXPECT_EQ("0,\"No error\"", Err());
}

TEST_F(ScpiInstrumentTest, MeasurementsFollowTheLink)
{
    EXPECT_EQ("9.91E+37", Q("MEAS:ESN0?"));
    h.Run(3000);
    EXPECT_EQ("1", Q("MEAS:LOCK?"));
    EXPECT_GT(D("MEAS:ESN0?"), 25.0);
    Q("MEAS:RES");
    EXPECT_EQ("9.91E+37", Q("MEAS:BER?")); // nothing measured yet in the new window
    h.Run(3000);
    EXPECT_EQ("0;0", Q("MEAS:BER?;FER?"));
    EXPECT_GE(D("MEAS:LOAD?"), 0.0);
    EXPECT_NE(std::string::npos, Q("MEAS:LAT?").find(','));
    EXPECT_NE(std::string::npos, Q("MEAS:COUN?").find(','));
    EXPECT_EQ(10U, Elements(Q("SYST:COUN?")));
}

TEST_F(ScpiInstrumentTest, ModcodAndAcm)
{
    h.Run(2000);
    Q("MOD:MODC 3");
    h.Run(2000);
    EXPECT_EQ("3", Q("MOD:MODC?"));
    EXPECT_EQ("0", Q("MOD:ACM?"));
    Q("MOD:ACM:LIM 1,2;MARG 2 dB;HYST 0.5;:MOD:ACM ON");
    EXPECT_EQ("0,\"No error\"", Err());
    EXPECT_EQ("1,2;2;0.5;1", Q("MOD:ACM:LIM?;MARG?;HYST?;:MOD:ACM:STAT?"));
    h.Run(8000);
    EXPECT_EQ("2", Q("MEAS:MODC?")); // clean link: the top of the allowed range
    Q("MOD:ACM:LIM 3,1");
    Q("MOD:MODC 5");
    Q("MOD:ACM:MARG 11");
    EXPECT_EQ("-221,\"Settings conflict;minimum above maximum\"", Err());
    EXPECT_EQ("-222,\"Data out of range;5\"", Err());
    EXPECT_EQ("-222,\"Data out of range;11\"", Err());
}

TEST_F(ScpiInstrumentTest, ChannelSettings)
{
    EXPECT_EQ("9.91E+37", Q("CHAN:ESN0?")); // no noise
    Q("CHAN:ESN0 10");
    EXPECT_EQ("1295", Q("CHAN:NOIS?"));
    EXPECT_EQ("10", Q("CHAN:ESN0?"));
    Q("CHAN:GAIN 16384");
    EXPECT_EQ("16384;4", Q("CHAN:GAIN?;ESN0?"));
    Q("CHAN:NOIS 0");
    EXPECT_EQ("0", Q("CHAN:NOIS?"));
    Q("CHAN:GAIN 0");
    EXPECT_EQ("-9.9E+37", Q("CHAN:ESN0?")); // no signal: minus infinity
    Q("CHAN:CLE");
    EXPECT_EQ("0;32767", Q("CHAN:NOIS?;GAIN?"));
    Q("CHAN:NOIS 70000");
    EXPECT_EQ("-222,\"Data out of range;70000\"", Err());
}

TEST_F(ScpiInstrumentTest, DeepFadeLosesLockAndRstRestoresTheLink)
{
    h.Run(2000);
    Q("MOD:MODC 4;:CHAN:ESN0 3");
    h.Run(4000);
    EXPECT_EQ("0", Q("MEAS:LOCK?"));
    Q("*RST");
    h.Run(6000);
    EXPECT_EQ("1", Q("MEAS:LOCK?"));
    EXPECT_EQ("1", Q("MOD:ACM?"));
    EXPECT_EQ("SOFT", Q("MOD:LOOP?"));
}

TEST_F(ScpiInstrumentTest, LoopbackAndRestart)
{
    Q("MOD:LOOP DIG");
    EXPECT_EQ("DIG", Q("MOD:LOOP?"));
    Q("MOD:LOOP ANAL");
    EXPECT_EQ("ANAL", Q("MOD:LOOP?"));
    Q("MOD:LOOP SIDEWAYS");
    EXPECT_EQ("-224,\"Illegal parameter value;SIDEWAYS\"", Err());
    Q("MOD:REST");
    EXPECT_EQ("-241,\"Hardware missing\"", Err());
}

TEST_F(ScpiInstrumentTest, RestartUsesTheHook)
{
    payload::PayloadConfig cfg;
    int restarts = 0;
    int result = 0;
    cfg.restart_modem = [&]() {
        ++restarts;
        return result;
    };
    PayloadHarness h2(cfg);
    payload::ScpiInstrument s2(h2.manager, "1", "1");
    s2.Execute("MOD:REST");
    EXPECT_EQ(1, restarts);
    result = -5;
    s2.Execute("MOD:REST");
    EXPECT_EQ("-200,\"Execution error;modem port error -5\"", s2.Execute("SYST:ERR?"));
}

TEST_F(ScpiInstrumentTest, PassEmulation)
{
    h.Run(1000);
    EXPECT_EQ("0", Q("PASS:STAT?").substr(0, 1));
    Q("PASS:STAR 80 DEG,20 DB,25");
    EXPECT_EQ("0,\"No error\"", Err());
    h.Run(15000);
    const std::string state = Q("PASS:STAT?");
    EXPECT_EQ("1,", state.substr(0, 2));
    EXPECT_EQ(5U, Elements(state));
    Q("PASS:STOP");
    EXPECT_EQ("0", Q("PASS:STAT?").substr(0, 1));
    Q("PASS:STAR");
    EXPECT_EQ("1", Q("PASS:STAT?").substr(0, 1));
    Q("PASS:STAR 5");
    Q("PASS:STAR 60,99");
    Q("PASS:STAR 60,10,0");
    Q("PASS:STAR 1,2,3,4");
    EXPECT_EQ("-222,\"Data out of range;5\"", Err());
    EXPECT_EQ("-222,\"Data out of range;99\"", Err());
    EXPECT_EQ("-222,\"Data out of range;0\"", Err());
    EXPECT_EQ("-108,\"Parameter not allowed\"", Err());
}

} // namespace
} // namespace satlink::test
