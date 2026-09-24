/**
 * @file main.cpp
 * @brief satlink-diag: exercise the frame accelerator, spectrum tap and audio codec on the board,
 *        and update the housekeeping controller over CAN.
 *
 * Built only for SATLINK_TARGET=linux. All command logic is in diag.cpp; this file only supplies
 * the real device nodes.
 *
 * @implements SRS-DIAG-001
 * @implements SRS-HKC-005
 */
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "satlink/diag/device_factory.hpp"
#include "satlink/diag/diag.hpp"
#include "satlink/hal/i2c_dev_bus.hpp"
#include "satlink/hal/posix_char_device.hpp"
#include "satlink/hal/socket_can_port.hpp"

namespace {

class PosixDeviceFactory final : public satlink::diag::DeviceFactory
{
  public:
    std::unique_ptr<satlink::hal::CharDeviceIo> OpenCharDevice(const std::string &path) override
    {
        return std::make_unique<satlink::hal::PosixCharDevice>(path);
    }

    std::unique_ptr<satlink::hal::I2cBus> OpenI2c(const std::string &path,
                                                  std::uint8_t address) override
    {
        return std::make_unique<satlink::hal::I2cDevBus>(path, address);
    }

    std::unique_ptr<satlink::hal::CanPort>
    OpenCan(const std::string &interface, const std::vector<std::uint32_t> &accept_ids) override
    {
        return std::make_unique<satlink::hal::SocketCanPort>(interface, accept_ids);
    }

    std::vector<std::uint8_t> ReadFile(const std::string &path) override
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            throw std::system_error(errno, std::generic_category(), "open(" + path + ")");
        }
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
};

} // namespace

int main(int argc, char **argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
    {
        args.emplace_back(argv[i]);
    }

    PosixDeviceFactory factory;
    return satlink::diag::RunDiag(args, factory, std::cout, std::cerr);
}
