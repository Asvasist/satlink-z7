/**
 * @file strip_chart.hpp
 * @brief A scrolling time chart (last N seconds) with one or two line series.
 */
#pragma once

#include <QChartView>
#include <QLineSeries>
#include <QValueAxis>

class QAbstractAxis;

class StripChart : public QChartView
{
    Q_OBJECT

  public:
    /// @p log_scale: logarithmic Y axis (BER); @p tick_interval: Y grid step (0 = automatic).
    StripChart(const QString &title, const QString &unit, double y_min, double y_max,
               bool log_scale = false, double tick_interval = 0.0, QWidget *parent = nullptr);

    /// Adds a series; returns its index.
    int AddSeries(const QString &name, const QColor &color, bool step = false);
    void Append(int series, double t_s, double value);
    void SetWindow(double seconds)
    {
        window_s_ = seconds;
    }

  private:
    struct Line
    {
        QLineSeries *series;
        bool step;
        double last = 0.0;
        bool has_last = false;
    };
    QChart *chart_;
    QValueAxis *x_;
    QAbstractAxis *y_;
    QList<Line> lines_;
    double window_s_ = 180.0;
    bool log_;
};
