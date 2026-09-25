/**
 * @file main_window.hpp
 * @brief Ground station main window: commanding on the left, live link plots in the middle,
 *        event log, telecommand history and housekeeping at the bottom.
 */
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <optional>

#include "gs_link.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class StripChart;
class ConstellationView;

class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);

    /// Connects to the payload with the values in the connection bar.
    bool ConnectLink();
    void SetPayloadAddress(const QString &host, quint16 tc_port, quint16 tm_port);
    GsLink &Link()
    {
        return link_;
    }
    /// Starts a pass with the values in the pass panel.
    void StartPass();
    /// Asks the payload for the constellation report (HK SID 4).
    void EnableConstellation();

  private:
    QWidget *BuildConnectionBar();
    QWidget *BuildCommandPanel();
    QWidget *BuildPlots();
    QWidget *BuildLogs();

    void OnTcSent(quint16 seq, const QString &label);
    void OnModemHk(const satlink::gs::ModemHk &hk);
    void OnLinkHk(const satlink::gs::LinkHk &hk);
    void OnPlatformHk(const satlink::gs::PlatformHk &hk);
    void OnConstellation(const satlink::gs::ConstellationHk &hk);
    void OnEvent(const satlink::gs::EventReport &event, qint64 unix_ms);
    void OnVerification(const satlink::gs::VerificationReport &report);
    void SetHk(QTreeWidgetItem *group, const QString &name, const QString &value);
    [[nodiscard]] double Now() const;

    GsLink link_;
    QElapsedTimer clock_;

    // connection bar
    QLineEdit *host_ = nullptr;
    QSpinBox *tc_port_ = nullptr;
    QSpinBox *tm_port_ = nullptr;
    QPushButton *connect_ = nullptr;
    QLabel *lock_ = nullptr;
    QLabel *modcod_ = nullptr;
    QLabel *esn0_ = nullptr;
    QLabel *pass_ = nullptr;
    QLabel *counters_ = nullptr;

    // commanding
    QComboBox *fixed_modcod_ = nullptr;
    QCheckBox *acm_on_ = nullptr;
    QComboBox *acm_min_ = nullptr;
    QComboBox *acm_max_ = nullptr;
    QDoubleSpinBox *acm_margin_ = nullptr;
    QDoubleSpinBox *acm_hyst_ = nullptr;
    QDoubleSpinBox *channel_esn0_ = nullptr;
    QDoubleSpinBox *pass_max_el_ = nullptr;
    QDoubleSpinBox *pass_zenith_ = nullptr;
    QSpinBox *pass_scale_ = nullptr;
    QComboBox *loopback_ = nullptr;
    QCheckBox *hk_enable_[4] = {};

    // plots
    StripChart *esn0_chart_ = nullptr;
    StripChart *modcod_chart_ = nullptr;
    StripChart *ber_chart_ = nullptr;
    StripChart *geometry_chart_ = nullptr;
    ConstellationView *constellation_ = nullptr;

    // logs
    QTableWidget *events_ = nullptr;
    QTableWidget *tcs_ = nullptr;
    QTreeWidget *hk_ = nullptr;
    QTreeWidgetItem *hk_groups_[3] = {};
    QHash<quint16, int> tc_rows_;

    std::optional<satlink::gs::ModemHk> last_modem_;
};
