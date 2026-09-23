/**
 * @file main.cpp
 * @brief satlink-diag: exercise the frame accelerator, spectrum tap and audio codec on the board.
 *
 * Built only for SATLINK_TARGET=linux. All command logic is in diag.cpp; this file only supplies
 * the real device nodes.
 *
 * @implements SRS-DIAG-001
 */
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "satlink/diag/device_factory.hpp"
#include "satlink/diag/diag.hpp"
#include "satlink/hal/i2c_dev_bus.hpp"
#include "satlink/hal/posix_char_device.hpp"

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
