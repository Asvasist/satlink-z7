"""Checks of the performance-report tool (threshold extraction, 1 % FER interpolation).

@verifies SRS-PERF-001
"""
import importlib.util
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("plot_perf", HERE.parent / "plot_perf.py")
plot_perf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(plot_perf)


def rows(points):
    return [{"esn0_db": db, "fer": fer, "frames": 500.0} for db, fer in points]


def test_thresholds_come_from_the_modem_table():
    t = plot_perf.thresholds()
    assert len(t) == 5 and t == sorted(t)


def test_required_esn0_interpolates_in_log_domain():
    r = rows([(1.0, 1.0), (2.0, 0.1), (3.0, 0.001)])
    assert math.isclose(plot_perf.required_esn0(r), 2.5)
    # A later bad point wins: the FER must stay low from there on.
    r = rows([(1.0, 0.5), (2.0, 0.0), (3.0, 0.02), (4.0, 0.0)])
    assert 3.0 < plot_perf.required_esn0(r) < 4.0
    assert plot_perf.required_esn0(rows([(1.0, 0.0)])) == 1.0
    assert plot_perf.required_esn0(rows([(1.0, 0.5)])) is None


def test_summary_flags_a_threshold_below_the_need(tmp_path):
    csv = tmp_path / "perf.csv"
    header = "modcod,esn0_db,frames,frame_errors,missed,fer,bits,bit_errors,ber,esn0_est_db\n"
    lines = [header]
    for m, (bad, good) in enumerate([(1, 2), (3, 4), (6, 7), (9, 10), (14, 15)]):
        lines.append(f"{m},{bad},500,500,0,1,0,0,0.5,{bad}\n")
        lines.append(f"{m},{good},500,0,0,0,1000,0,0,{good}\n")
    csv.write_text("".join(lines))
    table, ok = plot_perf.summary(plot_perf.load(csv), plot_perf.thresholds())
    assert not ok  # 8PSK 5/6 needs ~15 dB here, more than its threshold
    assert "8PSK 5/6" in table


def test_shannon_limit():
    assert math.isclose(plot_perf.shannon_esn0(1.0), 0.0, abs_tol=1e-12)
