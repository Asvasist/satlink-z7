/**
 * @file parser.hpp
 * @brief SCPI-1999 / IEEE 488.2 program message parser.
 *
 * A command is registered with its SCPI header pattern, e.g.
 *
 *     parser.Add("MEASure:ESN0?", handler);          // query
 *     parser.Add("MODem:ACM[:STATe]", handler);      // [optional node]
 *
 * Upper-case letters are the short form; a received header node matches the short form or the
 * whole long form, case-insensitively ("MEAS", "meas", "MEASURE", not "MEASU"). A program
 * message holds several commands separated by ';'. A command that does not start with ':' or
 * '*' is relative to the node of the previous command in the same message
 * ("MOD:ACM:MARG 1;HYST 0.5"). Query responses of one message are joined with ';'.
 *
 * Errors go into the error queue (SYSTem:ERRor?) with their SCPI numbers and set the matching
 * bit of the standard event status register (*ESR?). The parser handles the IEEE 488.2 common
 * commands *CLS, *ESE, *ESR?, *OPC, *OPC?, *WAI, *STB?, *SRE and SYSTem:ERRor[:NEXT]?,
 * SYSTem:ERRor:COUNt?, SYSTem:VERSion?; the application adds *IDN?, *RST, *TST? and its own
 * tree.
 *
 * @implements SRS-SCPI-001
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace satlink::scpi {

/// SCPI error numbers used here (SCPI-1999 volume 2, chapter 21).
enum class Error : int
{
    kNone = 0,
    kCommand = -100,
    kSyntax = -102,
    kDataType = -104,
    kParameterNotAllowed = -108,
    kMissingParameter = -109,
    kUndefinedHeader = -113,
    kExecution = -200,
    kSettingsConflict = -221,
    kDataOutOfRange = -222,
    kIllegalParameterValue = -224,
    kHardwareMissing = -241,
    kQueueOverflow = -350,
    kQueryError = -400,
};

/// Default message of an error number ("Undefined header").
std::string_view ErrorText(Error e);

/// Standard event status register bits (IEEE 488.2 11.5.1).
enum EsrBit : std::uint8_t
{
    kEsrOpc = 0x01,
    kEsrQueryError = 0x04,
    kEsrDeviceError = 0x08,
    kEsrExecutionError = 0x10,
    kEsrCommandError = 0x20,
};

/// One command being executed: its parameters and the response being built.
class Call
{
  public:
    Call(std::span<const std::string> params, bool query) : params_(params), query_(query) {}

    [[nodiscard]] std::size_t Count() const
    {
        return params_.size();
    }
    [[nodiscard]] bool IsQuery() const
    {
        return query_;
    }
    [[nodiscard]] const std::string &Raw(std::size_t i) const
    {
        return params_[i];
    }

    /// Numeric parameter @p i in [lo, hi]; MINimum, MAXimum and DEFault map to lo, hi and @p def.
    /// An optional unit suffix listed in @p units is accepted ("6.5 DB"). Reports the error and
    /// returns nullopt when the value is missing, not numeric or out of range.
    std::optional<double> Number(std::size_t i, double lo, double hi,
                                 std::optional<double> def = std::nullopt,
                                 std::initializer_list<std::string_view> units = {});
    /// Integer parameter (rejects fractions).
    std::optional<long> Integer(std::size_t i, long lo, long hi,
                                std::optional<long> def = std::nullopt);
    /// Boolean parameter: ON, OFF, 1, 0.
    std::optional<bool> Boolean(std::size_t i);
    /// Character parameter matched against @p choices (SCPI short/long form); returns the index.
    std::optional<std::size_t> Choice(std::size_t i,
                                      std::initializer_list<std::string_view> choices);
    /// Fails with -109 / -108 unless the call has between @p min and @p max parameters.
    bool Expect(std::size_t min, std::size_t max);

    /// Append a response element; elements are separated by ','.
    void Reply(std::string_view text);
    void Reply(double value);
    void Reply(long value);
    void Reply(bool value);
    /// Quoted string response ("abc", with inner quotes doubled).
    void ReplyString(std::string_view text);

    /// Reports an error; the parser adds it to the queue after the handler returns.
    void Fail(Error e, std::string_view detail = {});

    [[nodiscard]] const std::string &Response() const
    {
        return response_;
    }
    [[nodiscard]] const std::vector<std::pair<Error, std::string>> &Errors() const
    {
        return errors_;
    }

  private:
    std::span<const std::string> params_;
    bool query_;
    std::string response_;
    std::vector<std::pair<Error, std::string>> errors_;
};

class Parser
{
  public:
    using Handler = std::function<void(Call &)>;
    static constexpr std::size_t kQueueSize = 16;
    static constexpr std::size_t kMaxLine = 4096;

    Parser();

    /// Registers a command. A trailing '?' makes it a query; register set and query separately.
    void Add(std::string_view pattern, Handler handler);

    /// Executes one program message (one line, without the terminator). Returns the response
    /// line without terminator, empty if the message held no query.
    std::string Execute(std::string_view line);

    /// Adds an error to the queue (for errors detected outside a handler).
    void PushError(Error e, std::string_view detail = {});
    /// Oldest error as "<n>,\"<text>\"", or 0,"No error".
    std::string PopError();
    [[nodiscard]] std::size_t ErrorCount() const
    {
        return errors_.size();
    }
    [[nodiscard]] std::uint8_t Esr() const
    {
        return esr_;
    }
    /// Status byte: bit 2 error queue not empty, bit 5 ESB (ESR & ESE), bit 6 MSS.
    [[nodiscard]] std::uint8_t Stb() const;

    /// *CLS: clears the error queue and the event register.
    void Clear();

  private:
    struct Node
    {
        std::string long_form; ///< upper case
        std::size_t short_len = 0;
        bool optional = false;
    };
    struct Command
    {
        std::vector<Node> nodes;
        bool query = false;
        bool common = false; ///< *XXX
        Handler handler;
    };

    void ExecuteOne(std::string_view unit, std::vector<std::string> &path, std::string &response);
    [[nodiscard]] const Command *Find(const std::vector<std::string> &header, bool query,
                                      bool common) const;
    static bool Match(const std::vector<Node> &nodes, std::size_t ni,
                      const std::vector<std::string> &header, std::size_t hi);
    void AddCommon();

    std::vector<Command> commands_;
    std::deque<std::pair<Error, std::string>> errors_;
    std::uint8_t esr_ = 0;
    std::uint8_t ese_ = 0;
    std::uint8_t sre_ = 0;
};

/// Splits @p text at unquoted @p sep characters ("a,'b,c'" -> a | 'b,c').
std::vector<std::string_view> SplitUnquoted(std::string_view text, char sep);

} // namespace satlink::scpi
