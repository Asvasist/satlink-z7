/**
 * @file main_window.cpp
 * @implements SRS-GS-001
 */
#include "main_window.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <array>

#include "constellation_view.hpp"
#include "strip_chart.hpp"

namespace {

using satlink::gs::ModcodName;

QComboBox *ModcodCombo(int initial)
{
    auto *c = new QComboBox;
    for (std::uint8_t m = 0; m < 5; ++m)
    {
        c->addItem(QString("%1  %2").arg(m).arg(QString::fromStdString(ModcodName(m))), m);
    }
    c->setCurrentIndex(initial);
    return c;
}

QDoubleSpinBox *Spin(double lo, double hi, double value, double step, const QString &suffix)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setValue(value);
    s->setSingleStep(step);
    s->setDecimals(1);
    s->setSuffix(suffix);
    return s;
}

QLabel *Indicator(const QString &text)
{
    auto *l = new QLabel(text);
    l->setAlignment(Qt::AlignCenter);
    l->setMinimumWidth(110);
    l->setFrameShape(QFrame::StyledPanel);
    l->setMargin(3);
    return l;
}

void SetIndicator(QLabel *l, const QString &text, const char *color)
{
    l->setText(text);
    l->setStyleSheet(QString("QLabel { background: %1; color: white; font-weight: bold; }")
                         .arg(QLatin1String(color)));
}

QTableWidget *Table(const QStringList &headers)
{
    auto *t = new QTableWidget(0, static_cast<int>(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->horizontalHeader()->setStretchLastSection(true);
    t->verticalHeader()->setVisible(false);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    return t;
}

constexpr std::array<const char *, 5> kSeverity = {"", "info", "low", "medium", "high"};

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("SatLink-Z7 Ground Station"));
    clock_.start();

    auto *central = new QWidget;
    auto *outer = new QVBoxLayout(central);
    outer->addWidget(BuildConnectionBar());

    auto *vertical = new QSplitter(Qt::Vertical);
    auto *horizontal = new QSplitter(Qt::Horizontal);
    auto *scroll = new QScrollArea;
    scroll->setWidget(BuildCommandPanel());
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(300);
    horizontal->addWidget(scroll);
    horizontal->addWidget(BuildPlots());
    horizontal->setStretchFactor(1, 1);
    vertical->addWidget(horizontal);
    vertical->addWidget(BuildLogs());
    vertical->setStretchFactor(0, 3);
    vertical->setStretchFactor(1, 1);
    outer->addWidget(vertical, 1);
    setCentralWidget(central);
    statusBar()->showMessage(tr("Not connected"));
    resize(1400, 900);

    connect(&link_, &GsLink::TcSent, this, &MainWindow::OnTcSent);
    connect(&link_, &GsLink::ModemHk, this, &MainWindow::OnModemHk);
    connect(&link_, &GsLink::LinkHk, this, &MainWindow::OnLinkHk);
    connect(&link_, &GsLink::PlatformHk, this, &MainWindow::OnPlatformHk);
    connect(&link_, &GsLink::Constellation, this, &MainWindow::OnConstellation);
    connect(&link_, &GsLink::Event, this, &MainWindow::OnEvent);
    connect(&link_, &GsLink::Verification, this, &MainWindow::OnVerification);
    connect(&link_, &GsLink::Pong, this,
            [this]() { statusBar()->showMessage(tr("Payload alive (TM[17,2])"), 5000); });
}

double MainWindow::Now() const
{
    return static_cast<double>(clock_.elapsed()) / 1000.0;
}

QWidget *MainWindow::BuildConnectionBar()
{
    auto *bar = new QWidget;
    auto *l = new QHBoxLayout(bar);
    l->setContentsMargins(0, 0, 0, 0);
    host_ = new QLineEdit("127.0.0.1");
    host_->setMaximumWidth(160);
    tc_port_ = new QSpinBox;
    tc_port_->setRange(1, 65535);
    tc_port_->setValue(satlink::payload::kTcPort);
    tm_port_ = new QSpinBox;
    tm_port_->setRange(1, 65535);
    tm_port_->setValue(satlink::payload::kTmPort);
    connect_ = new QPushButton(tr("Connect"));
    connect(connect_, &QPushButton::clicked, this, [this]() { ConnectLink(); });
    l->addWidget(new QLabel(tr("Payload")));
    l->addWidget(host_);
    l->addWidget(new QLabel(tr("TC port")));
    l->addWidget(tc_port_);
    l->addWidget(new QLabel(tr("TM port")));
    l->addWidget(tm_port_);
    l->addWidget(connect_);
    l->addStretch(1);
    lock_ = Indicator(tr("NO TM"));
    modcod_ = Indicator("MODCOD -");
    esn0_ = Indicator("Es/N0 -");
    pass_ = Indicator(tr("no pass"));
    counters_ = new QLabel;
    SetIndicator(lock_, tr("NO TM"), "#777777");
    for (QLabel *w : {lock_, modcod_, esn0_, pass_})
    {
        l->addWidget(w);
    }
    l->addWidget(counters_);
    return bar;
}

void MainWindow::SetPayloadAddress(const QString &host, quint16 tc_port, quint16 tm_port)
{
    host_->setText(host);
    tc_port_->setValue(tc_port);
    tm_port_->setValue(tm_port);
}

bool MainWindow::ConnectLink()
{
    QString error;
    if (!link_.Open(host_->text(), static_cast<quint16>(tc_port_->value()),
                    static_cast<quint16>(tm_port_->value()), &error))
    {
        statusBar()->showMessage(tr("Connection failed: %1").arg(error));
        return false;
    }
    statusBar()->showMessage(tr("Commanding %1:%2, telemetry on UDP %3")
                                 .arg(host_->text())
                                 .arg(tc_port_->value())
                                 .arg(tm_port_->value()));
    connect_->setText(tr("Reconnect"));
    link_.Send(link_.Cmd().Ping(), tr("Are-you-alive"));
    return true;
}

QWidget *MainWindow::BuildCommandPanel()
{
    auto *panel = new QWidget;
    auto *v = new QVBoxLayout(panel);
    auto send = [this](std::vector<std::uint8_t> tc, const QString &label) {
        if (!link_.IsOpen())
        {
            statusBar()->showMessage(tr("Connect first"), 3000);
            return;
        }
        link_.Send(tc, label);
    };

    auto *test = new QGroupBox(tr("Test (ST[17])"));
    auto *test_l = new QHBoxLayout(test);
    auto *ping = new QPushButton(tr("Are you alive?"));
    connect(ping, &QPushButton::clicked, this,
            [this, send]() { send(link_.Cmd().Ping(), tr("Are-you-alive")); });
    test_l->addWidget(ping);
    v->addWidget(test);

    auto *mod = new QGroupBox(tr("Modulation and coding"));
    auto *mod_l = new QFormLayout(mod);
    fixed_modcod_ = ModcodCombo(1);
    auto *set_fixed = new QPushButton(tr("Fixed MODCOD (ACM off)"));
    connect(set_fixed, &QPushButton::clicked, this, [this, send]() {
        const auto m = static_cast<std::uint8_t>(fixed_modcod_->currentData().toInt());
        send(link_.Cmd().SetModcod(m), tr("Set MODCOD %1").arg(m));
    });
    acm_on_ = new QCheckBox(tr("Adaptive (ACM)"));
    acm_on_->setChecked(true);
    acm_min_ = ModcodCombo(0);
    acm_max_ = ModcodCombo(4);
    acm_margin_ = Spin(0.0, 10.0, 1.0, 0.5, " dB");
    acm_hyst_ = Spin(0.0, 10.0, 1.0, 0.5, " dB");
    auto *apply_acm = new QPushButton(tr("Apply ACM"));
    connect(apply_acm, &QPushButton::clicked, this, [this, send]() {
        send(link_.Cmd().SetAcm(acm_on_->isChecked(),
                                static_cast<std::uint8_t>(acm_min_->currentData().toInt()),
                                static_cast<std::uint8_t>(acm_max_->currentData().toInt()),
                                acm_margin_->value(), acm_hyst_->value()),
             tr("ACM %1").arg(acm_on_->isChecked() ? "on" : "off"));
    });
    mod_l->addRow(tr("MODCOD"), fixed_modcod_);
    mod_l->addRow(set_fixed);
    mod_l->addRow(acm_on_);
    mod_l->addRow(tr("Lowest"), acm_min_);
    mod_l->addRow(tr("Highest"), acm_max_);
    mod_l->addRow(tr("Margin"), acm_margin_);
    mod_l->addRow(tr("Hysteresis"), acm_hyst_);
    mod_l->addRow(apply_acm);
    v->addWidget(mod);

    auto *chan = new QGroupBox(tr("Channel emulator"));
    auto *chan_l = new QFormLayout(chan);
    channel_esn0_ = Spin(-5.0, 40.0, 12.0, 0.5, " dB");
    auto *set_chan = new QPushButton(tr("Set Es/N0"));
    connect(set_chan, &QPushButton::clicked, this, [this, send]() {
        const double db = channel_esn0_->value();
        send(link_.Cmd().SetChannel(satlink::gs::NoiseLevelFor(db), 0x7FFF),
             tr("Channel %1 dB").arg(db, 0, 'f', 1));
    });
    auto *clear_chan = new QPushButton(tr("Clear (no noise)"));
    connect(clear_chan, &QPushButton::clicked, this,
            [this, send]() { send(link_.Cmd().SetChannel(0, 0x7FFF), tr("Channel clear")); });
    chan_l->addRow(tr("Es/N0"), channel_esn0_);
    chan_l->addRow(set_chan);
    chan_l->addRow(clear_chan);
    v->addWidget(chan);

    auto *pass = new QGroupBox(tr("LEO pass"));
    auto *pass_l = new QFormLayout(pass);
    pass_max_el_ = Spin(6.0, 90.0, 60.0, 5.0, QString::fromUtf8(" °"));
    pass_zenith_ = Spin(-10.0, 40.0, 20.0, 1.0, " dB");
    pass_scale_ = new QSpinBox;
    pass_scale_->setRange(1, 255);
    pass_scale_->setValue(10);
    pass_scale_->setSuffix(" x");
    auto *start = new QPushButton(tr("Start pass"));
    connect(start, &QPushButton::clicked, this, &MainWindow::StartPass);
    auto *stop = new QPushButton(tr("Stop pass"));
    connect(stop, &QPushButton::clicked, this,
            [this, send]() { send(link_.Cmd().StopPass(), tr("Stop pass")); });
    pass_l->addRow(tr("Max elevation"), pass_max_el_);
    pass_l->addRow(tr("Es/N0 at zenith"), pass_zenith_);
    pass_l->addRow(tr("Time lapse"), pass_scale_);
    pass_l->addRow(start);
    pass_l->addRow(stop);
    v->addWidget(pass);

    auto *modem = new QGroupBox(tr("Modem"));
    auto *modem_l = new QFormLayout(modem);
    loopback_ = new QComboBox;
    loopback_->addItems({tr("Analog (DAC -> ADC)"), tr("Digital (PL)"), tr("Software")});
    loopback_->setCurrentIndex(2);
    auto *set_loop = new QPushButton(tr("Set loopback"));
    connect(set_loop, &QPushButton::clicked, this, [this, send]() {
        send(link_.Cmd().SetLoopback(static_cast<std::uint8_t>(loopback_->currentIndex())),
             tr("Loopback %1").arg(loopback_->currentText()));
    });
    auto *restart = new QPushButton(tr("Restart Core 1 firmware"));
    connect(restart, &QPushButton::clicked, this, [this, send]() {
        if (QMessageBox::question(this, tr("Restart modem"),
                                  tr("Restart the real-time modem firmware?")) == QMessageBox::Yes)
        {
            send(link_.Cmd().RestartModem(), tr("Restart modem"));
        }
    });
    modem_l->addRow(tr("Loopback"), loopback_);
    modem_l->addRow(set_loop);
    modem_l->addRow(restart);
    v->addWidget(modem);

    auto *hk = new QGroupBox(tr("Housekeeping (ST[03])"));
    auto *hk_l = new QVBoxLayout(hk);
    const std::array<QString, 4> names = {tr("1 Modem"), tr("2 Link"), tr("3 Platform"),
                                          tr("4 Constellation (about 1 kbit/s)")};
    for (int i = 0; i < 4; ++i)
    {
        hk_enable_[i] = new QCheckBox(names[static_cast<std::size_t>(i)]);
        hk_enable_[i]->setChecked(i < 3); // the payload's defaults
        const auto sid = static_cast<std::uint8_t>(i + 1);
        connect(hk_enable_[i], &QCheckBox::toggled, this, [this, send, sid](bool on) {
            const std::array<std::uint8_t, 1> sids{sid};
            send(link_.Cmd().EnableHk(sids, on), tr("HK %1 %2").arg(sid).arg(on ? "on" : "off"));
        });
        hk_l->addWidget(hk_enable_[i]);
    }
    auto *oneshot = new QPushButton(tr("Report all now"));
    connect(oneshot, &QPushButton::clicked, this, [this, send]() {
        const std::array<std::uint8_t, 4> sids{1, 2, 3, 4};
        send(link_.Cmd().OneShotHk(sids), tr("HK one-shot"));
    });
    hk_l->addWidget(oneshot);
    v->addWidget(hk);
    v->addStretch(1);
    return panel;
}

void MainWindow::StartPass()
{
    if (!link_.IsOpen())
    {
        return;
    }
    link_.Send(link_.Cmd().StartPass(pass_max_el_->value(), pass_zenith_->value(),
                                     static_cast<std::uint8_t>(pass_scale_->value())),
               tr("Pass %1 deg, %2 dB, x%3")
                   .arg(pass_max_el_->value(), 0, 'f', 0)
                   .arg(pass_zenith_->value(), 0, 'f', 0)
                   .arg(pass_scale_->value()));
}

void MainWindow::EnableConstellation()
{
    hk_enable_[3]->setChecked(true);
}

QWidget *MainWindow::BuildPlots()
{
    auto *w = new QWidget;
    auto *g = new QGridLayout(w);
    g->setContentsMargins(0, 0, 0, 0);
    esn0_chart_ = new StripChart(tr("Es/N0"), "dB", -5, 40, false, 5);
    esn0_chart_->AddSeries(tr("measured"), QColor(0x1f, 0x77, 0xb4));
    esn0_chart_->AddSeries(tr("pass model"), QColor(0xff, 0x7f, 0x0e));
    modcod_chart_ = new StripChart(tr("MODCOD"), tr("index"), -0.5, 4.5, false, 1);
    modcod_chart_->AddSeries(tr("MODCOD"), QColor(0x2c, 0xa0, 0x2c), true);
    ber_chart_ = new StripChart(tr("Bit error rate (1 s)"), "BER", 1e-7, 1e-1, true);
    ber_chart_->AddSeries(tr("BER"), QColor(0xd6, 0x27, 0x28));
    ber_chart_->AddSeries(tr("upper bound (no errors)"), QColor(0x99, 0x99, 0x99));
    geometry_chart_ = new StripChart(tr("Pass geometry"), tr("elevation [deg]"), 0, 90, false, 15);
    geometry_chart_->AddSeries(tr("elevation"), QColor(0x94, 0x67, 0xbd));
    constellation_ = new ConstellationView;
    g->addWidget(esn0_chart_, 0, 0);
    g->addWidget(modcod_chart_, 0, 1);
    g->addWidget(ber_chart_, 1, 0);
    g->addWidget(geometry_chart_, 1, 1);
    g->addWidget(constellation_, 0, 2, 2, 1);
    g->setColumnStretch(0, 3);
    g->setColumnStretch(1, 3);
    g->setColumnStretch(2, 2);
    return w;
}

QWidget *MainWindow::BuildLogs()
{
    auto *tabs = new QTabWidget;
    events_ = Table({tr("Time (UTC)"), tr("Severity"), tr("Event")});
    events_->setColumnWidth(0, 110);
    tcs_ = Table({tr("Seq"), tr("Sent"), tr("Command"), tr("Acceptance"), tr("Completion")});
    tcs_->setColumnWidth(2, 220);
    hk_ = new QTreeWidget;
    hk_->setHeaderLabels({tr("Parameter"), tr("Value")});
    hk_->setColumnWidth(0, 260);
    const std::array<QString, 3> groups = {tr("Modem (SID 1)"), tr("Link (SID 2)"),
                                           tr("Platform (SID 3)")};
    for (int i = 0; i < 3; ++i)
    {
        hk_groups_[i] = new QTreeWidgetItem(hk_, {groups[static_cast<std::size_t>(i)]});
        hk_groups_[i]->setExpanded(true);
    }
    tabs->addTab(events_, tr("Events"));
    tabs->addTab(tcs_, tr("Telecommands"));
    tabs->addTab(hk_, tr("Housekeeping"));
    return tabs;
}

void MainWindow::SetHk(QTreeWidgetItem *group, const QString &name, const QString &value)
{
    for (int i = 0; i < group->childCount(); ++i)
    {
        if (group->child(i)->text(0) == name)
        {
            group->child(i)->setText(1, value);
            return;
        }
    }
    new QTreeWidgetItem(group, {name, value});
}

void MainWindow::OnTcSent(quint16 seq, const QString &label)
{
    const int row = tcs_->rowCount();
    tcs_->insertRow(row);
    tcs_->setItem(row, 0, new QTableWidgetItem(QString::number(seq)));
    tcs_->setItem(row, 1,
                  new QTableWidgetItem(QDateTime::currentDateTimeUtc().toString("HH:mm:ss")));
    tcs_->setItem(row, 2, new QTableWidgetItem(label));
    tcs_->setItem(row, 3, new QTableWidgetItem("..."));
    tcs_->setItem(row, 4, new QTableWidgetItem("..."));
    tc_rows_.insert(seq, row);
    tcs_->scrollToBottom();
}

void MainWindow::OnVerification(const satlink::gs::VerificationReport &r)
{
    const auto it = tc_rows_.constFind(r.sequence_count);
    if (it == tc_rows_.constEnd())
    {
        return;
    }
    const int column = r.subtype <= 2 ? 3 : 4;
    QString text = r.Success() ? tr("OK") : tr("FAILED");
    if (r.failure)
    {
        text += ": " + QString::fromStdString(satlink::gs::FailureName(*r.failure));
    }
    auto *item = new QTableWidgetItem(text);
    item->setForeground(r.Success() ? QColor(0x2c, 0xa0, 0x2c) : QColor(0xd6, 0x27, 0x28));
    tcs_->setItem(*it, column, item);
    if (!r.Success() && column == 3)
    {
        tcs_->setItem(*it, 4, new QTableWidgetItem("-"));
    }
}

void MainWindow::OnModemHk(const satlink::gs::ModemHk &hk)
{
    const double t = Now();
    SetIndicator(lock_, hk.locked ? tr("LOCKED") : tr("NO LOCK"),
                 hk.locked ? "#2ca02c" : "#d62728");
    SetIndicator(modcod_, QString::fromStdString(ModcodName(hk.modcod)) + (hk.acm ? " ACM" : ""),
                 "#1f77b4");
    SetIndicator(esn0_, QString("Es/N0 %1 dB").arg(hk.esn0_db, 0, 'f', 1), "#555555");
    if (hk.locked)
    {
        esn0_chart_->Append(0, t, hk.esn0_db);
    }
    modcod_chart_->Append(0, t, hk.modcod);
    if (last_modem_ && hk.bits_checked > last_modem_->bits_checked)
    {
        const double bits = hk.bits_checked - last_modem_->bits_checked;
        const double errors = hk.bit_errors - last_modem_->bit_errors;
        if (errors > 0)
        {
            ber_chart_->Append(0, t, errors / bits);
        }
        else
        {
            ber_chart_->Append(1, t, 1.0 / bits);
        }
    }
    last_modem_ = hk;

    auto *g = hk_groups_[0];
    SetHk(g, tr("Receiver lock"), hk.locked ? tr("yes") : tr("no"));
    SetHk(g, tr("MODCOD"), QString::fromStdString(ModcodName(hk.modcod)));
    SetHk(g, tr("ACM"), hk.acm ? tr("on") : tr("off"));
    SetHk(g, tr("Es/N0"), QString("%1 dB").arg(hk.esn0_db, 0, 'f', 2));
    SetHk(g, tr("Frames OK / CRC error / header error"),
          QString("%1 / %2 / %3").arg(hk.frames_ok).arg(hk.frames_crc_error).arg(hk.header_errors));
    SetHk(g, tr("Bit errors / bits checked"),
          QString("%1 / %2").arg(hk.bit_errors).arg(hk.bits_checked));
    SetHk(g, tr("RX latency max / avg"),
          QString("%1 / %2 us").arg(hk.latency_max_us).arg(hk.latency_avg_us));
    SetHk(g, tr("Core 1 load"), QString("%1 %").arg(hk.cpu_load_pct, 0, 'f', 1));
    SetHk(g, tr("TX queue"), QString::number(hk.tx_queue));
    SetHk(g, tr("Modem uptime"), QString("%1 s").arg(hk.uptime_ms / 1000));
    counters_->setText(tr("TM %1").arg(link_.TmCount()));
}

void MainWindow::OnLinkHk(const satlink::gs::LinkHk &hk)
{
    const double t = Now();
    if (hk.pass_active)
    {
        SetIndicator(pass_, QString::fromUtf8("EL %1°").arg(hk.elevation_deg, 0, 'f', 1),
                     hk.elevation_deg > 5.0 ? "#9467bd" : "#777777");
        esn0_chart_->Append(1, t, hk.elevation_deg > 5.0 ? hk.pass_esn0_db : -5.0);
        geometry_chart_->Append(0, t, std::max(0.0, hk.elevation_deg));
    }
    else
    {
        SetIndicator(pass_, tr("no pass"), "#777777");
    }
    auto *g = hk_groups_[1];
    SetHk(g, tr("Pass"), hk.pass_active ? tr("active") : tr("-"));
    SetHk(g, tr("Elevation / range"),
          QString("%1 deg / %2 km").arg(hk.elevation_deg, 0, 'f', 2).arg(hk.range_km, 0, 'f', 0));
    SetHk(g, tr("Range rate"), QString("%1 km/s").arg(hk.range_rate_km_s, 0, 'f', 3));
    SetHk(g, tr("Frames sent / received / lost"),
          QString("%1 / %2 / %3").arg(hk.frames_sent).arg(hk.frames_received).arg(hk.lost_frames));
    SetHk(g, tr("Packets / resyncs / dropped"),
          QString("%1 / %2 / %3").arg(hk.packets).arg(hk.resyncs).arg(hk.dropped));
    SetHk(g, tr("IP datagrams down / up"), QString("%1 / %2").arg(hk.ip_down).arg(hk.ip_up));
}

void MainWindow::OnPlatformHk(const satlink::gs::PlatformHk &hk)
{
    static const std::array<QString, 5> kStates = {tr("offline"), tr("booting"), tr("running"),
                                                   tr("fault"), tr("crashed")};
    auto *g = hk_groups_[2];
    SetHk(g, tr("Housekeeping controller"), hk.hkc_valid ? tr("reporting") : tr("silent"));
    SetHk(g, tr("FPGA temperature"), QString::fromUtf8("%1 °C").arg(hk.temperature_c, 0, 'f', 1));
    SetHk(g, tr("VCCINT / VCCAUX / VCCBRAM"),
          QString("%1 / %2 / %3 mV").arg(hk.vccint_mv).arg(hk.vccaux_mv).arg(hk.vbram_mv));
    SetHk(g, tr("HKC uptime / error flags"),
          QString("%1 s / 0x%2").arg(hk.hkc_uptime_s).arg(hk.hkc_error_flags, 2, 16, QChar('0')));
    SetHk(g, tr("Core 1 state / restarts"),
          QString("%1 / %2")
              .arg(hk.rtos_state < kStates.size() ? kStates[hk.rtos_state]
                                                  : QString::number(hk.rtos_state))
              .arg(hk.rtos_restarts));
}

void MainWindow::OnConstellation(const satlink::gs::ConstellationHk &hk)
{
    constellation_->Show(hk);
}

void MainWindow::OnEvent(const satlink::gs::EventReport &e, qint64 unix_ms)
{
    const int row = events_->rowCount();
    events_->insertRow(row);
    events_->setItem(
        row, 0,
        new QTableWidgetItem(
            QDateTime::fromMSecsSinceEpoch(unix_ms, Qt::UTC).toString("HH:mm:ss.zzz")));
    auto *sev = new QTableWidgetItem(kSeverity[std::min<std::size_t>(e.severity, 4)]);
    if (e.severity >= 2)
    {
        sev->setForeground(e.severity >= 3 ? QColor(0xd6, 0x27, 0x28) : QColor(0xff, 0x7f, 0x0e));
    }
    events_->setItem(row, 1, sev);
    events_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(e.Describe())));
    events_->scrollToBottom();
}
