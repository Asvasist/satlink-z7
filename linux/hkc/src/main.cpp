/**
 * @file main.cpp
 * @brief satlink-hkc: housekeeping controller monitor, commands and CAN firmware loader.
 *
 * Built only for SATLINK_TARGET=linux; the command logic is in hkc_cli.cpp.
 *
 * @implements SRS-HKC-006
 */
#include <cerrno>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "satlink/hal/socket_can_bus.hpp"
#include "satlink/hkc/hkc_cli.hpp"

namespace {

class LinuxEnvironment final : public satlink::hkc::Environment
{
  public:
    std::unique_ptr<satlink::hal::CanBus> OpenCan(const std::string &interface) override
    {
        return std::make_unique<satlink::hal::SocketCanBus>(interface);
    }

    std::vector<std::uint8_t> ReadFile(const std::string &path) override
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            throw std::system_error(ENOENT, std::generic_category(), path);
        }
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    std::chrono::system_clock::time_point Now() override
    {
        return std::chrono::system_clock::now();
    }
};

} // namespace

int main(int argc, char **argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    LinuxEnvironment env;
    return satlink::hkc::RunHkc(args, env, std::cout, std::cerr);
}
