/**
 * @file gs_link.hpp
 * @brief UDP link to satlink-payloadd: telecommands out, telemetry in, decoded into signals.
 */
#pragma once

#include <QHostAddress>
#include <QObject>
#include <QUdpSocket>
#include <cstdint>
#include <vector>

#include "mission_tm.hpp"

class GsLink : public QObject
{
    Q_OBJECT

  public:
    explicit GsLink(QObject *parent = nullptr);

    /// Listens for TM on @p tm_port and sends TCs to @p host : @p tc_port.
    bool Open(const QString &host, quint16 tc_port, quint16 tm_port, QString *error);
    [[nodiscard]] bool IsOpen() const
    {
        return socket_.state() == QAbstractSocket::BoundState;
    }

    /// Sends a telecommand built by the Commander; returns its sequence count.
    quint16 Send(const std::vector<std::uint8_t> &tc, const QString &label);

    satlink::gs::Commander &Cmd()
    {
        return cmd_;
    }

    [[nodiscard]] quint32 TmCount() const
    {
        return tm_count_;
    }
    [[nodiscard]] quint32 BadTmCount() const
    {
        return bad_tm_;
    }

  signals:
    void TcSent(quint16 seq, const QString &label);
    void ModemHk(const satlink::gs::ModemHk &hk);
    void LinkHk(const satlink::gs::LinkHk &hk);
    void PlatformHk(const satlink::gs::PlatformHk &hk);
    void Constellation(const satlink::gs::ConstellationHk &hk);
    void Event(const satlink::gs::EventReport &event, qint64 unix_ms);
    void Verification(const satlink::gs::VerificationReport &report);
    void Pong();

  private:
    void OnReadyRead();

    QUdpSocket socket_;
    QHostAddress payload_;
    quint16 tc_port_ = 0;
    satlink::gs::Commander cmd_;
    quint32 tm_count_ = 0;
    quint32 bad_tm_ = 0;
};
