/**
 * @file main.cpp
 * @brief satlink-gs: the SatLink-Z7 ground station.
 *
 *   satlink-gs [--payload HOST[:TC_PORT]] [--tm-port N] [--connect] [--pass] [--constellation]
 *              [--screenshot FILE --after SECONDS]
 *
 * --connect opens the link at start-up, --pass also starts a LEO pass, --constellation turns on
 * the constellation report; --screenshot saves the window after the given time and quits (runs
 * headless with QT_QPA_PLATFORM=offscreen).
 *
 * @implements SRS-GS-001
 * @implements SRS-GS-002
 */
#include <QApplication>
#include <QCommandLineParser>
#include <QPixmap>
#include <QTimer>

#include "main_window.hpp"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("satlink-gs");
    QApplication::setApplicationVersion(SATLINK_GS_VERSION);

    QCommandLineParser cli;
    cli.setApplicationDescription("SatLink-Z7 ground station");
    cli.addHelpOption();
    cli.addVersionOption();
    const QCommandLineOption payload("payload", "Payload address HOST[:TC_PORT].", "address",
                                     "127.0.0.1:10025");
    const QCommandLineOption tm_port("tm-port", "Local UDP port for telemetry.", "port", "10026");
    const QCommandLineOption do_connect("connect", "Connect at start-up.");
    const QCommandLineOption pass("pass", "Start a LEO pass after connecting.");
    const QCommandLineOption constellation("constellation",
                                           "Request the constellation report after connecting.");
    const QCommandLineOption screenshot("screenshot", "Save a screenshot and quit.", "file");
    const QCommandLineOption after("after", "Seconds before the screenshot.", "seconds", "20");
    cli.addOptions({payload, tm_port, do_connect, pass, constellation, screenshot, after});
    cli.process(app);

    MainWindow window;
    const QStringList address = cli.value(payload).split(':');
    window.SetPayloadAddress(address.value(0),
                             static_cast<quint16>(address.value(1, "10025").toUInt()),
                             static_cast<quint16>(cli.value(tm_port).toUInt()));
    window.show();
    if (cli.isSet(do_connect) || cli.isSet(pass) || cli.isSet(screenshot))
    {
        const bool connected = window.ConnectLink();
        if (connected && cli.isSet(constellation))
        {
            window.EnableConstellation();
        }
        if (connected && cli.isSet(pass))
        {
            QTimer::singleShot(3000, &window, [&window]() { window.StartPass(); });
        }
    }
    if (cli.isSet(screenshot))
    {
        const QString file = cli.value(screenshot);
        QTimer::singleShot(static_cast<int>(cli.value(after).toDouble() * 1000.0), &window,
                           [&window, file]() {
                               window.grab().save(file);
                               QApplication::quit();
                           });
    }
    return QApplication::exec();
}
