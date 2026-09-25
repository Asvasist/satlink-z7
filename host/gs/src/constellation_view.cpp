/**
 * @file constellation_view.cpp
 * @implements SRS-GS-001
 */
#include "constellation_view.hpp"

#include <QValueAxis>
#include <cmath>
#include <numbers>

ConstellationView::ConstellationView(QWidget *parent)
    : QChartView(parent), chart_(new QChart), ideal_(new QScatterSeries),
      received_(new QScatterSeries)
{
    chart_->setTitle(tr("Constellation (HK SID 4)"));
    chart_->legend()->setAlignment(Qt::AlignBottom);
    chart_->setMargins(QMargins(4, 4, 4, 4));
    ideal_->setName(tr("ideal"));
    ideal_->setMarkerSize(14);
    ideal_->setColor(QColor(0xcc, 0xcc, 0xcc));
    ideal_->setBorderColor(QColor(0x99, 0x99, 0x99));
    received_->setName(tr("received"));
    received_->setMarkerSize(6);
    received_->setColor(QColor(0x1f, 0x77, 0xb4));
    received_->setBorderColor(QColor(0x1f, 0x77, 0xb4));
    chart_->addSeries(ideal_);
    chart_->addSeries(received_);
    auto *x = new QValueAxis;
    auto *y = new QValueAxis;
    for (QValueAxis *a : {x, y})
    {
        a->setRange(-1.6, 1.6);
        a->setTickCount(5);
        a->setLabelFormat("%.1f");
    }
    x->setTitleText("I");
    y->setTitleText("Q");
    chart_->addAxis(x, Qt::AlignBottom);
    chart_->addAxis(y, Qt::AlignLeft);
    for (QScatterSeries *s : {ideal_, received_})
    {
        s->attachAxis(x);
        s->attachAxis(y);
    }
    setChart(chart_);
    setRenderHint(QPainter::Antialiasing);
    setMinimumSize(300, 300);
}

void ConstellationView::Show(const satlink::gs::ConstellationHk &hk)
{
    if (hk.modcod != modcod_)
    {
        modcod_ = hk.modcod;
        ideal_->clear();
        // As mapped by the modem (libs/modem/src/mapper.c): BPSK on the I axis, QPSK on the
        // diagonals, 8PSK from 0 degrees in steps of 45.
        const int m = hk.modcod == 0 ? 2 : (hk.modcod <= 2 ? 4 : 8);
        const double offset = m == 4 ? std::numbers::pi / 4.0 : 0.0;
        for (int k = 0; k < m; ++k)
        {
            const double a = offset + (2.0 * std::numbers::pi * k / m);
            ideal_->append(std::cos(a), std::sin(a));
        }
        chart_->setTitle(tr("Constellation: %1")
                             .arg(QString::fromStdString(satlink::gs::ModcodName(hk.modcod))));
    }
    QList<QPointF> pts;
    pts.reserve(static_cast<qsizetype>(hk.points.size()));
    for (const auto &[i, q] : hk.points)
    {
        pts.append(QPointF(i, q));
    }
    received_->replace(pts);
}
