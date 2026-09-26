/**
 * @file parser.cpp
 * @implements SRS-SCPI-001
 */
#include "satlink/scpi/parser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace satlink::scpi {
namespace {

/// SCPI representations of NaN and of the infinities.
constexpr std::string_view kNan = "9.91E+37";
constexpr std::string_view kInf = "9.9E+37";
constexpr std::string_view kMinusInf = "-9.9E+37";

char Upper(char c)
{
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

bool IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view Trim(std::string_view s)
{
    while (!s.empty() && IsSpace(s.front()))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && IsSpace(s.back()))
    {
        s.remove_suffix(1);
    }
    return s;
}

std::string ToUpper(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), Upper);
    return out;
}

/// Short form = leading upper-case letters and digits of the pattern word ("MEASure" -> 4); a
/// word written in lower case has only its long form.
std::size_t ShortLength(std::string_view word)
{
    std::size_t n = 0;
    while (n < word.size() &&
           (std::isupper(static_cast<unsigned char>(word[n])) != 0 ||
            std::isdigit(static_cast<unsigned char>(word[n])) != 0 || word[n] == '_'))
    {
        ++n;
    }
    return n == 0 ? word.size() : n;
}

/// Received mnemonic @p got (upper case) against a pattern word.
bool MatchWord(std::string_view long_form, std::size_t short_len, std::string_view got)
{
    return got == long_form || got == long_form.substr(0, short_len);
}

bool ValidMnemonic(std::string_view s)
{
    return !s.empty() && std::isalpha(static_cast<unsigned char>(s.front())) != 0 &&
           std::all_of(s.begin(), s.end(), [](char c) {
               return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
           });
}

std::uint8_t EsrBitFor(Error e)
{
    const int n = static_cast<int>(e);
    if (n <= -100 && n > -200)
    {
        return kEsrCommandError;
    }
    if (n <= -200 && n > -300)
    {
        return kEsrExecutionError;
    }
    if (n <= -300 && n > -400)
    {
        return kEsrDeviceError;
    }
    if (n <= -400 && n > -500)
    {
        return kEsrQueryError;
    }
    return 0;
}

std::string FormatError(Error e, const std::string &detail)
{
    std::string text(ErrorText(e));
    if (!detail.empty())
    {
        text += ';';
        text += detail;
    }
    std::string quoted;
    for (const char c : text)
    {
        quoted += c;
        if (c == '"')
        {
            quoted += '"';
        }
    }
    return std::to_string(static_cast<int>(e)) + ",\"" + quoted + "\"";
}

} // namespace

std::string_view ErrorText(Error e)
{
    switch (e)
    {
    case Error::kNone:
        return "No error";
    case Error::kCommand:
        return "Command error";
    case Error::kSyntax:
        return "Syntax error";
    case Error::kDataType:
        return "Data type error";
    case Error::kParameterNotAllowed:
        return "Parameter not allowed";
    case Error::kMissingParameter:
        return "Missing parameter";
    case Error::kUndefinedHeader:
        return "Undefined header";
    case Error::kExecution:
        return "Execution error";
    case Error::kSettingsConflict:
        return "Settings conflict";
    case Error::kDataOutOfRange:
        return "Data out of range";
    case Error::kIllegalParameterValue:
        return "Illegal parameter value";
    case Error::kHardwareMissing:
        return "Hardware missing";
    case Error::kQueueOverflow:
        return "Queue overflow";
    case Error::kQueryError:
        return "Query error";
    }
    return "Unknown error";
}

std::vector<std::string_view> SplitUnquoted(std::string_view text, char sep)
{
    std::vector<std::string_view> out;
    char quote = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (quote != 0)
        {
            if (c == quote)
            {
                quote = 0; // a doubled quote closes and reopens: same result
            }
        }
        else if (c == '"' || c == '\'')
        {
            quote = c;
        }
        else if (c == sep)
        {
            out.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    out.push_back(text.substr(start));
    return out;
}

// ---- Call ----

void Call::Fail(Error e, std::string_view detail)
{
    errors_.emplace_back(e, std::string(detail));
}

bool Call::Expect(std::size_t min, std::size_t max)
{
    if (params_.size() < min)
    {
        Fail(Error::kMissingParameter);
        return false;
    }
    if (params_.size() > max)
    {
        Fail(Error::kParameterNotAllowed);
        return false;
    }
    return true;
}

std::optional<double> Call::Number(std::size_t i, double lo, double hi, std::optional<double> def,
                                   std::initializer_list<std::string_view> units)
{
    if (i >= params_.size())
    {
        Fail(Error::kMissingParameter);
        return std::nullopt;
    }
    const std::string word = ToUpper(params_[i]);
    if (word == "MIN" || word == "MINIMUM")
    {
        return lo;
    }
    if (word == "MAX" || word == "MAXIMUM")
    {
        return hi;
    }
    if ((word == "DEF" || word == "DEFAULT") && def)
    {
        return def;
    }
    const char *begin = params_[i].c_str();
    char *end = nullptr;
    errno = 0;
    const double v = std::strtod(begin, &end);
    if (end == begin || errno != 0 || !std::isfinite(v))
    {
        Fail(Error::kDataType, params_[i]);
        return std::nullopt;
    }
    const std::string suffix = ToUpper(Trim(std::string_view(end)));
    if (!suffix.empty() && std::none_of(units.begin(), units.end(),
                                        [&](std::string_view u) { return ToUpper(u) == suffix; }))
    {
        Fail(Error::kDataType, params_[i]);
        return std::nullopt;
    }
    if (v < lo || v > hi)
    {
        Fail(Error::kDataOutOfRange, params_[i]);
        return std::nullopt;
    }
    return v;
}

std::optional<long> Call::Integer(std::size_t i, long lo, long hi, std::optional<long> def)
{
    const auto v = Number(i, static_cast<double>(lo), static_cast<double>(hi),
                          def ? std::optional<double>(static_cast<double>(*def)) : std::nullopt);
    if (!v)
    {
        return std::nullopt;
    }
    if (std::floor(*v) != *v)
    {
        Fail(Error::kDataType, params_[i]);
        return std::nullopt;
    }
    return static_cast<long>(*v);
}

std::optional<bool> Call::Boolean(std::size_t i)
{
    if (i >= params_.size())
    {
        Fail(Error::kMissingParameter);
        return std::nullopt;
    }
    const std::string word = ToUpper(params_[i]);
    if (word == "ON" || word == "1")
    {
        return true;
    }
    if (word == "OFF" || word == "0")
    {
        return false;
    }
    Fail(Error::kIllegalParameterValue, params_[i]);
    return std::nullopt;
}

std::optional<std::size_t> Call::Choice(std::size_t i,
                                        std::initializer_list<std::string_view> choices)
{
    if (i >= params_.size())
    {
        Fail(Error::kMissingParameter);
        return std::nullopt;
    }
    const std::string word = ToUpper(params_[i]);
    std::size_t index = 0;
    for (const auto c : choices)
    {
        if (MatchWord(ToUpper(c), ShortLength(c), word))
        {
            return index;
        }
        ++index;
    }
    Fail(Error::kIllegalParameterValue, params_[i]);
    return std::nullopt;
}

void Call::Reply(std::string_view text)
{
    if (!response_.empty())
    {
        response_ += ',';
    }
    response_ += text;
}

void Call::Reply(double value)
{
    if (std::isnan(value))
    {
        Reply(kNan);
        return;
    }
    if (std::isinf(value))
    {
        Reply(value > 0 ? kInf : kMinusInf);
        return;
    }
    std::array<char, 32> buf{};
    const int n = std::snprintf(buf.data(), buf.size(), "%.6G", value);
    Reply(std::string_view(buf.data(), n > 0 ? static_cast<std::size_t>(n) : 0U));
}

void Call::Reply(long value)
{
    Reply(std::string_view(std::to_string(value)));
}

void Call::Reply(bool value)
{
    Reply(std::string_view(value ? "1" : "0"));
}

void Call::ReplyString(std::string_view text)
{
    std::string quoted = "\"";
    for (const char c : text)
    {
        quoted += c;
        if (c == '"')
        {
            quoted += '"';
        }
    }
    quoted += '"';
    Reply(std::string_view(quoted));
}

// ---- Parser ----

Parser::Parser()
{
    AddCommon();
}

void Parser::Add(std::string_view pattern, Handler handler)
{
    Command cmd;
    cmd.query = !pattern.empty() && pattern.back() == '?';
    if (cmd.query)
    {
        pattern.remove_suffix(1);
    }
    cmd.common = !pattern.empty() && pattern.front() == '*';
    cmd.handler = std::move(handler);
    if (cmd.common)
    {
        cmd.nodes.push_back({ToUpper(pattern.substr(1)), pattern.size() - 1, false});
        commands_.push_back(std::move(cmd));
        return;
    }
    // "[:A]B:C[:D]": nodes separated by ':', a node in brackets is optional.
    std::size_t i = 0;
    while (i < pattern.size())
    {
        bool optional = false;
        if (pattern[i] == '[')
        {
            optional = true;
            ++i;
        }
        if (i < pattern.size() && pattern[i] == ':')
        {
            ++i;
        }
        std::size_t j = i;
        while (j < pattern.size() && pattern[j] != ':' && pattern[j] != '[' && pattern[j] != ']')
        {
            ++j;
        }
        const std::string_view word = pattern.substr(i, j - i);
        cmd.nodes.push_back({ToUpper(word), ShortLength(word), optional});
        i = j;
        if (i < pattern.size() && pattern[i] == ']')
        {
            ++i;
        }
    }
    commands_.push_back(std::move(cmd));
}

bool Parser::Match(const std::vector<Node> &nodes, std::size_t ni,
                   const std::vector<std::string> &header, std::size_t hi)
{
    if (ni == nodes.size())
    {
        return hi == header.size();
    }
    const Node &n = nodes[ni];
    if (hi < header.size() && MatchWord(n.long_form, n.short_len, header[hi]) &&
        Match(nodes, ni + 1, header, hi + 1))
    {
        return true;
    }
    return n.optional && Match(nodes, ni + 1, header, hi);
}

const Parser::Command *Parser::Find(const std::vector<std::string> &header, bool query,
                                    bool common) const
{
    const auto it = std::find_if(commands_.begin(), commands_.end(), [&](const Command &c) {
        return c.query == query && c.common == common && Match(c.nodes, 0, header, 0);
    });
    return it != commands_.end() ? &*it : nullptr;
}

std::string Parser::Execute(std::string_view line)
{
    std::string response;
    if (line.size() > kMaxLine)
    {
        PushError(Error::kCommand, "program message too long");
        return response;
    }
    std::vector<std::string> path;
    for (const auto unit : SplitUnquoted(line, ';'))
    {
        const std::string_view u = Trim(unit);
        if (!u.empty())
        {
            ExecuteOne(u, path, response);
        }
    }
    return response;
}

void Parser::ExecuteOne(std::string_view unit, std::vector<std::string> &path,
                        std::string &response)
{
    std::size_t split = 0;
    while (split < unit.size() && !IsSpace(unit[split]))
    {
        ++split;
    }
    std::string_view head = unit.substr(0, split);
    const std::string_view rest = Trim(unit.substr(split));

    const bool query = head.back() == '?';
    if (query)
    {
        head.remove_suffix(1);
    }
    const bool common = !head.empty() && head.front() == '*';
    const bool absolute = !head.empty() && head.front() == ':';
    if (common || absolute)
    {
        head.remove_prefix(1);
    }

    std::vector<std::string> header;
    for (const auto word : SplitUnquoted(head, ':'))
    {
        if (!ValidMnemonic(word))
        {
            PushError(Error::kSyntax, unit);
            return;
        }
        header.push_back(ToUpper(word));
    }
    if (common && header.size() != 1)
    {
        PushError(Error::kSyntax, unit);
        return;
    }

    const Command *cmd = nullptr;
    std::vector<std::string> resolved;
    if (!common && !absolute && !path.empty())
    {
        resolved = path;
        resolved.insert(resolved.end(), header.begin(), header.end());
        cmd = Find(resolved, query, false);
    }
    if (cmd == nullptr)
    {
        resolved = header;
        cmd = Find(resolved, query, common);
    }
    if (cmd == nullptr)
    {
        PushError(Error::kUndefinedHeader, unit);
        return;
    }
    if (!common)
    {
        path.assign(resolved.begin(), resolved.end() - 1);
    }

    std::vector<std::string> params;
    if (!rest.empty())
    {
        for (const auto p : SplitUnquoted(rest, ','))
        {
            const std::string_view v = Trim(p);
            if (v.empty())
            {
                PushError(Error::kSyntax, unit);
                return;
            }
            params.emplace_back(v);
        }
    }
    Call call(params, query);
    cmd->handler(call);
    for (const auto &[e, detail] : call.Errors())
    {
        PushError(e, detail);
    }
    if (query && call.Errors().empty())
    {
        if (!response.empty())
        {
            response += ';';
        }
        response += call.Response();
    }
}

void Parser::PushError(Error e, std::string_view detail)
{
    esr_ = static_cast<std::uint8_t>(esr_ | EsrBitFor(e));
    if (errors_.size() >= kQueueSize)
    {
        errors_.back() = {Error::kQueueOverflow, std::string()};
        return;
    }
    errors_.emplace_back(e, std::string(detail));
}

std::string Parser::PopError()
{
    if (errors_.empty())
    {
        return FormatError(Error::kNone, std::string());
    }
    auto [e, detail] = std::move(errors_.front());
    errors_.pop_front();
    return FormatError(e, detail);
}

std::uint8_t Parser::Stb() const
{
    std::uint8_t stb = 0;
    if (!errors_.empty())
    {
        stb |= 0x04U;
    }
    if ((esr_ & ese_) != 0U)
    {
        stb |= 0x20U;
    }
    if ((stb & sre_ & 0xBFU) != 0U)
    {
        stb |= 0x40U;
    }
    return stb;
}

void Parser::Clear()
{
    errors_.clear();
    esr_ = 0;
}

void Parser::AddCommon()
{
    Add("*CLS", [this](Call &c) {
        if (c.Expect(0, 0))
        {
            Clear();
        }
    });
    Add("*ESE", [this](Call &c) {
        if (const auto v = c.Integer(0, 0, 255); v && c.Expect(1, 1))
        {
            ese_ = static_cast<std::uint8_t>(*v);
        }
    });
    Add("*ESE?", [this](Call &c) { c.Reply(static_cast<long>(ese_)); });
    Add("*ESR?", [this](Call &c) {
        c.Reply(static_cast<long>(esr_));
        esr_ = 0;
    });
    Add("*SRE", [this](Call &c) {
        if (const auto v = c.Integer(0, 0, 255); v && c.Expect(1, 1))
        {
            sre_ = static_cast<std::uint8_t>(*v);
        }
    });
    Add("*SRE?", [this](Call &c) { c.Reply(static_cast<long>(sre_)); });
    Add("*STB?", [this](Call &c) { c.Reply(static_cast<long>(Stb())); });
    // Commands execute in order and complete before the next one is parsed.
    Add("*OPC", [this](Call &) { esr_ = static_cast<std::uint8_t>(esr_ | kEsrOpc); });
    Add("*OPC?", [](Call &c) { c.Reply(true); });
    Add("*WAI", [](Call &) {});
    Add("SYSTem:ERRor[:NEXT]?", [this](Call &c) { c.Reply(std::string_view(PopError())); });
    Add("SYSTem:ERRor:COUNt?", [this](Call &c) { c.Reply(static_cast<long>(errors_.size())); });
    Add("SYSTem:ERRor:ALL?", [this](Call &c) {
        c.Reply(std::string_view(PopError())); // 0,"No error" when empty
        while (!errors_.empty())
        {
            c.Reply(std::string_view(PopError()));
        }
    });
    Add("SYSTem:VERSion?", [](Call &c) { c.Reply(std::string_view("1999.0")); });
}

} // namespace satlink::scpi
