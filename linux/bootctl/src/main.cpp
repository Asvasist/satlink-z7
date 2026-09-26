/**
 * @file main.cpp
 * @brief satlink-bootctl: A/B slot control on the target, through libubootenv's fw_printenv and
 *        fw_setenv (configured by /etc/fw_env.config to read uboot.env on the boot partition).
 *
 * @implements SRS-BOOT-002
 */
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include "satlink/bootctl/bootctl_cli.hpp"

namespace {

std::string RunCapture(const char *command, int &status)
{
    std::string output;
    FILE *pipe = ::popen(command, "r"); // NOLINT(cert-env33-c): fixed command strings only
    if (pipe == nullptr)
    {
        status = -1;
        return output;
    }
    std::array<char, 256> buf{};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr)
    {
        output += buf.data();
    }
    status = ::pclose(pipe);
    return output;
}

class FwEnvStore final : public satlink::bootctl::EnvStore
{
  public:
    std::map<std::string, std::string> Read() override
    {
        int status = 0;
        const std::string text = RunCapture("fw_printenv 2>/dev/null", status);
        if (status != 0)
        {
            throw std::runtime_error("fw_printenv failed (check /etc/fw_env.config)");
        }
        std::map<std::string, std::string> env;
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line))
        {
            const auto eq = line.find('=');
            if (eq != std::string::npos)
            {
                env[line.substr(0, eq)] = line.substr(eq + 1);
            }
        }
        return env;
    }

    void Write(const std::map<std::string, std::string> &updates) override
    {
        // fw_setenv -s applies a whole script in one environment write.
        char path[] = "/tmp/satlink-bootctl.XXXXXX";
        const int fd = ::mkstemp(path);
        if (fd < 0)
        {
            throw std::runtime_error("mkstemp failed");
        }
        ::close(fd);
        {
            std::ofstream script(path);
            for (const auto &[key, value] : updates)
            {
                script << key << ' ' << value << '\n';
            }
        }
        const std::string command = std::string("fw_setenv -s ") + path;
        const int status = std::system(command.c_str()); // NOLINT(cert-env33-c)
        ::unlink(path);
        if (status != 0)
        {
            throw std::runtime_error("fw_setenv failed");
        }
    }
};

std::string ReadCmdline()
{
    std::ifstream file("/proc/cmdline");
    std::string cmdline;
    std::getline(file, cmdline);
    return cmdline;
}

bool SystemRunning()
{
    int status = 0;
    const std::string state = RunCapture("systemctl is-system-running --wait 2>/dev/null", status);
    return state == "running\n";
}

} // namespace

int main(int argc, char **argv)
{
    FwEnvStore env;
    satlink::bootctl::CliContext ctx{env, ReadCmdline(), &SystemRunning};
    return satlink::bootctl::RunBootctl(std::vector<std::string>(argv + 1, argv + argc), ctx,
                                        std::cout, std::cerr);
}
