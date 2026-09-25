/**
 * @file constellation_view.hpp
 * @brief Scatter plot of received symbols (HK SID 4) over the ideal points of their MODCOD.
 */
#pragma once

#include <QChartView>
#include <QScatterSeries>

#include "mission_tm.hpp"

class ConstellationView : public QChartView
{
    Q_OBJECT

  public:
    explicit ConstellationView(QWidget *parent = nullptr);
    void Show(const satlink::gs::ConstellationHk &hk);

  private:
    QChart *chart_;
    QScatterSeries *ideal_;
    QScatterSeries *received_;
    int modcod_ = -1;
};
