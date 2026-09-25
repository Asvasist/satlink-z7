/**
 * @file strip_chart.cpp
 * @implements SRS-GS-001
 */
#include "strip_chart.hpp"

#include <QLogValueAxis>
#include <QPen>
#include <algorithm>

StripChart::StripChart(const QString &title, const QString &unit, double y_min, double y_max,
                       bool log_scale, double tick_interval, QWidget *parent)
    : QChartView(parent), chart_(new QChart), x_(new QValueAxis), log_(log_scale)
{
    chart_->setTitle(title);
    chart_->legend()->setAlignment(Qt::AlignBottom);
    chart_->setMargins(QMargins(4, 4, 4, 4));
    x_->setTitleText(tr("time [s]"));
    x_->setLabelFormat("%.0f");
    x_->setTickType(QValueAxis::TicksDynamic);
    x_->setTickAnchor(0.0);
    x_->setTickInterval(30.0);
    x_->setRange(0, window_s_);
    if (log_scale)
    {
        auto *y = new QLogValueAxis;
        y->setBase(10);
        y->setLabelFormat("%.0e");
        y->setRange(y_min, y_max);
        y->setMinorTickCount(-1);
        y_ = y;
    }
    else
    {
        auto *y = new QValueAxis;
        y->setRange(y_min, y_max);
        y->setLabelFormat("%.0f");
        if (tick_interval > 0.0)
        {
            y->setTickType(QValueAxis::TicksDynamic);
            y->setTickAnchor(0.0);
            y->setTickInterval(tick_interval);
        }
        y_ = y;
    }
    y_->setTitleText(unit);
    chart_->addAxis(x_, Qt::AlignBottom);
    chart_->addAxis(y_, Qt::AlignLeft);
    setChart(chart_);
    setRenderHint(QPainter::Antialiasing);
    setMinimumSize(320, 200);
}

int StripChart::AddSeries(const QString &name, const QColor &color, bool step)
{
    auto *s = new QLineSeries;
    s->setName(name);
    QPen pen(color);
    pen.setWidthF(2.0);
    s->setPen(pen);
    chart_->addSeries(s);
    s->attachAxis(x_);
    s->attachAxis(y_);
    lines_.append({s, step});
    if (lines_.size() == 1)
    {
        chart_->legend()->setVisible(false);
    }
    else
    {
        chart_->legend()->setVisible(true);
    }
    return static_cast<int>(lines_.size() - 1);
}

void StripChart::Append(int series, double t_s, double value)
{
    Line &l = lines_[series];
    if (log_ && value <= 0.0)
    {
        return; // not representable
    }
    if (l.step && l.has_last)
    {
        l.series->append(t_s, l.last);
    }
    l.series->append(t_s, value);
    l.last = value;
    l.has_last = true;

    const double start = std::max(0.0, t_s - window_s_);
    for (auto &line : lines_)
    {
        int drop = 0;
        const auto pts = line.series->points();
        while (drop < pts.size() - 1 && pts[drop + 1].x() < start)
        {
            ++drop;
        }
        if (drop > 0)
        {
            line.series->removePoints(0, drop);
        }
    }
    x_->setRange(start, std::max(window_s_, t_s));
}
