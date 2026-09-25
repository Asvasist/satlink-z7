/**
 * @file test_posix_io.cpp
 * @brief The daemon's Linux I/O: systemd notifications and the SCPI TCP line server, over real
 *        sockets on localhost.
 *
 * @verifies SRS-FDIR-002
 * @verifies SRS-SYS-004
 */
#include <arpa/inet.h>
#include <array>
#include <cstdlib>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

#include "satlink/payload/posix_io.hpp"

namespace {

using satlink::payload::SystemdNotifier;
using satlink::payload::TcpLineServer;

class Fd
{
  public:
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd()
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
        }
    }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
    Fd(Fd &&) = delete;
    Fd &operator=(Fd &&) = delete;
    [[nodiscard]] int Get() const
    {
        return fd_;
    }

  private:
    int fd_;
};

std::string Receive(int fd)
{
    std::array<char, 256> buf{};
    const ssize_t n = ::recv(fd, buf.data(), buf.size(), MSG_DONTWAIT);
    return n > 0 ? std::string(buf.data(), static_cast<std::size_t>(n)) : std::string();
}

TEST(SystemdNotifierTest, SendsStatesToTheNotifySocket)
{
    const std::string path = "/tmp/satlink-notify-test-" + std::to_string(::getpid());
    Fd listener(::socket(AF_UNIX, SOCK_DGRAM, 0));
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    path.copy(addr.sun_path, sizeof(addr.sun_path) - 1);
    (void)::unlink(path.c_str());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): sockets API
    ASSERT_EQ(0, ::bind(listener.Get(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)));

    ASSERT_EQ(0, ::setenv("NOTIFY_SOCKET", path.c_str(), 1));
    ASSERT_EQ(0, ::setenv("WATCHDOG_USEC", "10000000", 1));
    {
        SystemdNotifier n;
        EXPECT_EQ(10000U, n.WatchdogMs());
        EXPECT_TRUE(n.Notify("READY=1"));
        EXPECT_TRUE(n.Notify("WATCHDOG=1"));
    }
    EXPECT_EQ("READY=1", Receive(listener.Get()));
    EXPECT_EQ("WATCHDOG=1", Receive(listener.Get()));

    ASSERT_EQ(0, ::unsetenv("NOTIFY_SOCKET"));
    ASSERT_EQ(0, ::unsetenv("WATCHDOG_USEC"));
    SystemdNotifier outside;
    EXPECT_FALSE(outside.Notify("READY=1"));
    EXPECT_EQ(0U, outside.WatchdogMs());
    (void)::unlink(path.c_str());
}

TEST(TcpLineServerTest, AnswersLinesFromSeveralClients)
{
    std::vector<std::string> lines;
    TcpLineServer server(0, [&](std::string_view line) {
        lines.emplace_back(line);
        return line == "silent" ? std::string() : "echo " + std::string(line);
    });
    ASSERT_NE(0, server.Port());

    auto connect_client = [&]() {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(server.Port());
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): sockets API
        EXPECT_EQ(0, ::connect(fd, reinterpret_cast<const sockaddr *>(&a), sizeof(a)));
        return fd;
    };
    Fd a(connect_client());
    Fd b(connect_client());
    const std::string first = "one\nsil";
    ASSERT_EQ(static_cast<ssize_t>(first.size()), ::send(a.Get(), first.data(), first.size(), 0));
    ASSERT_EQ(4, ::send(b.Get(), "two\n", 4, 0));
    for (int i = 0; i < 20 && lines.size() < 2; ++i)
    {
        server.Poll();
        ::usleep(5000);
    }
    EXPECT_EQ(2U, server.Clients());
    ASSERT_EQ(5, ::send(a.Get(), "ent\n\n", 5, 0));
    for (int i = 0; i < 20 && lines.size() < 4; ++i)
    {
        server.Poll();
        ::usleep(5000);
    }
    ASSERT_EQ(4U, lines.size());
    EXPECT_EQ("silent", lines[2]);
    EXPECT_EQ("", lines[3]);
    EXPECT_EQ("echo one\necho \n", Receive(a.Get()));
    EXPECT_EQ("echo two\n", Receive(b.Get()));

    std::vector<pollfd> fds;
    server.AddPollFds(fds);
    EXPECT_EQ(3U, fds.size());

    // An over-long line is dropped whole; the connection stays usable.
    const std::string huge(TcpLineServer::kMaxLine + 100, 'x');
    ASSERT_EQ(static_cast<ssize_t>(huge.size()), ::send(b.Get(), huge.data(), huge.size(), 0));
    ASSERT_EQ(3, ::send(b.Get(), "\nok", 3, 0));
    ASSERT_EQ(1, ::send(b.Get(), "\n", 1, 0));
    for (int i = 0; i < 40 && lines.size() < 5; ++i)
    {
        server.Poll();
        ::usleep(5000);
    }
    ASSERT_EQ(5U, lines.size());
    EXPECT_EQ("ok", lines[4]);

    // A closed client is removed.
    ::shutdown(a.Get(), SHUT_RDWR);
    for (int i = 0; i < 20 && server.Clients() > 1; ++i)
    {
        server.Poll();
        ::usleep(5000);
    }
    EXPECT_EQ(1U, server.Clients());
}

TEST(TcpLineServerTest, PortInUseThrows)
{
    TcpLineServer first(0, [](std::string_view) { return std::string(); });
    EXPECT_THROW(TcpLineServer(first.Port(), [](std::string_view) { return std::string(); }),
                 std::system_error);
}

} // namespace
