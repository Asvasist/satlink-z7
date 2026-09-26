/**
 * @file gs_link.cpp
 * @implements SRS-GS-001
 */
#include "gs_link.hpp"

#include <QHostInfo>
#include <QNetworkDatagram>
#include <span>
#include <variant>

GsLink::GsLink(QObject *parent) : QObject(parent)
{
    connect(&socket_, &QUdpSocket::readyRead, this, &GsLink::OnReadyRead);
}

bool GsLink::Open(const QString &host, quint16 tc_port, quint16 tm_port, QString *error)
{
    socket_.close();
    QHostAddress address(host);
    if (address.isNull())
    {
        const QHostInfo info = QHostInfo::fromName(host);
        for (const QHostAddress &a : info.addresses())
        {
            if (a.protocol() == QAbstractSocket::IPv4Protocol)
            {
                address = a;
                break;
            }
        }
    }
    if (address.isNull())
    {
        *error = tr("cannot resolve %1").arg(host);
        return false;
    }
    if (!socket_.bind(QHostAddress::AnyIPv4, tm_port))
    {
        *error = socket_.errorString();
        return false;
    }
    payload_ = address;
    tc_port_ = tc_port;
    return true;
}

quint16 GsLink::Send(const std::vector<std::uint8_t> &tc, const QString &label)
{
    const quint16 seq = static_cast<quint16>(((tc[2] & 0x3F) << 8) | tc[3]);
    socket_.writeDatagram(reinterpret_cast<const char *>(tc.data()), static_cast<qint64>(tc.size()),
                          payload_, tc_port_);
    emit TcSent(seq, label);
    return seq;
}

void GsLink::OnReadyRead()
{
    while (socket_.hasPendingDatagrams())
    {
        const QNetworkDatagram datagram = socket_.receiveDatagram();
        const QByteArray bytes = datagram.data();
        const std::span<const std::uint8_t> packet(
            reinterpret_cast<const std::uint8_t *>(bytes.constData()),
            static_cast<std::size_t>(bytes.size()));
        satlink::pus::Telemetry tm;
        if (satlink::pus::Decode(packet, tm) != satlink::pus::DecodeError::kNone)
        {
            ++bad_tm_;
            continue;
        }
        ++tm_count_;
        const qint64 unix_ms = static_cast<qint64>(tm.time.ToUnixMs());
        std::visit(
            [&](const auto &r) {
                using T = std::decay_t<decltype(r)>;
                if constexpr (std::is_same_v<T, satlink::gs::ModemHk>)
                    emit ModemHk(r);
                else if constexpr (std::is_same_v<T, satlink::gs::LinkHk>)
                    emit LinkHk(r);
                else if constexpr (std::is_same_v<T, satlink::gs::PlatformHk>)
                    emit PlatformHk(r);
                else if constexpr (std::is_same_v<T, satlink::gs::ConstellationHk>)
                    emit Constellation(r);
                else if constexpr (std::is_same_v<T, satlink::gs::EventReport>)
                    emit Event(r, unix_ms);
                else if constexpr (std::is_same_v<T, satlink::gs::VerificationReport>)
                    emit Verification(r);
                else if constexpr (std::is_same_v<T, satlink::gs::PingReport>)
                    emit Pong();
            },
            satlink::gs::Interpret(tm));
    }
}
