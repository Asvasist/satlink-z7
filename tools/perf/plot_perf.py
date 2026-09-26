#!/usr/bin/env python3
"""Plots and checks the modem performance sweep (satlink-modem-perf).

    plot_perf.py modem_perf.csv --out docs/performance        # figures + summary table
    plot_perf.py modem_perf.csv --check                       # CI: thresholds hold

For every MODCOD the Es/N0 at which the frame error rate falls to 1 % is interpolated
(log-linear between the sweep points) and compared with the switching threshold the ACM
controller uses (libs/modem/src/modcod.c). A threshold below the measured requirement would let
ACM pick a MODCOD the link cannot carry: --check fails in that case.

@implements SRS-PERF-001
"""
from __future__ import annotations

import argparse
import csv
import math
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAMES = ["BPSK 1/2", "QPSK 1/2", "QPSK 3/4", "8PSK 2/3", "8PSK 5/6"]
BITS_PER_SYMBOL = [0.5, 1.0, 1.5, 2.0, 2.5]
TARGET_FER = 0.01


def thresholds() -> list[float]:
    """ACM thresholds as compiled into the modem (modcod.c)."""
    text = (ROOT / "libs/modem/src/modcod.c").read_text(encoding="utf-8")
    values = [float(v) for v in re.findall(r"([0-9]+\.[0-9]+)F,\s*[0-9.]+F\s*}", text)]
    if len(values) != len(NAMES):
        raise SystemExit(f"cannot read the thresholds from modcod.c (found {values})")
    return values


def load(path: Path) -> dict[int, list[dict[str, float]]]:
    data: dict[int, list[dict[str, float]]] = defaultdict(list)
    with path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            data[int(row["modcod"])].append({k: float(v) for k, v in row.items()})
    for rows in data.values():
        rows.sort(key=lambda r: r["esn0_db"])
    return data


def required_esn0(rows: list[dict[str, float]], target: float = TARGET_FER) -> float | None:
    """Lowest Es/N0 from which the FER stays at or below the target."""
    last_bad = None
    for i, r in enumerate(rows):
        if r["fer"] > target:
            last_bad = i
    if last_bad is None:
        return rows[0]["esn0_db"]
    if last_bad == len(rows) - 1:
        return None
    a, b = rows[last_bad], rows[last_bad + 1]
    # Interpolate log10(FER); a clean point counts as half an error.
    fa = math.log10(a["fer"])
    fb = math.log10(max(b["fer"], 0.5 / b["frames"]))
    t = (fa - math.log10(target)) / (fa - fb) if fa != fb else 1.0
    return a["esn0_db"] + max(0.0, min(1.0, t)) * (b["esn0_db"] - a["esn0_db"])


def shannon_esn0(bits_per_symbol: float) -> float:
    return 10.0 * math.log10(2.0 ** bits_per_symbol - 1.0)


def summary(data, limits) -> tuple[str, bool]:
    lines = [
        "| MODCOD | Info bits/symbol | Es/N0 for FER 1 % | ACM threshold | Margin | Shannon limit | Gap to Shannon |",
        "|---|---|---|---|---|---|---|",
    ]
    ok = True
    for m, name in enumerate(NAMES):
        req = required_esn0(data[m]) if m in data else None
        shannon = shannon_esn0(BITS_PER_SYMBOL[m])
        if req is None:
            ok = False
            lines.append(f"| {m} {name} | {BITS_PER_SYMBOL[m]:.1f} | not reached | {limits[m]:.1f} dB | - | {shannon:.1f} dB | - |")
            continue
        margin = limits[m] - req
        ok = ok and margin >= 0.0
        lines.append(
            f"| {m} {name} | {BITS_PER_SYMBOL[m]:.1f} | {req:.1f} dB | {limits[m]:.1f} dB | "
            f"{margin:+.1f} dB | {shannon:.1f} dB | {req - shannon:.1f} dB |"
        )
    return "\n".join(lines), ok


def plot(data, limits, out: Path) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    colors = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd"]
    for metric, label, fname in (("fer", "Frame error rate", "fer.png"), ("ber", "Bit error rate (delivered frames)", "ber.png")):
        fig, ax = plt.subplots(figsize=(8, 5), dpi=120)
        for m, name in enumerate(NAMES):
            rows = [r for r in data.get(m, []) if r[metric] > 0]
            if rows:
                ax.semilogy([r["esn0_db"] for r in rows], [r[metric] for r in rows], "o-",
                            color=colors[m], label=name, markersize=3)
            ax.axvline(limits[m], color=colors[m], linestyle=":", linewidth=1)
        if metric == "fer":
            ax.axhline(TARGET_FER, color="grey", linestyle="--", linewidth=1)
        ax.set_xlabel("Es/N0 [dB]")
        ax.set_ylabel(label)
        ax.set_ylim(1e-4 if metric == "fer" else 1e-6, 1.0)
        ax.grid(True, which="both", alpha=0.3)
        ax.legend(title="dotted: ACM threshold")
        ax.set_title(f"SatLink-Z7 software modem: {label.lower()}")
        fig.tight_layout()
        fig.savefig(out / fname)
        plt.close(fig)

    fig, ax = plt.subplots(figsize=(8, 5), dpi=120)
    for m, name in enumerate(NAMES):
        rows = [r for r in data.get(m, []) if not math.isnan(r["esn0_est_db"])]
        ax.plot([r["esn0_db"] for r in rows], [r["esn0_est_db"] - r["esn0_db"] for r in rows],
                "o-", color=colors[m], label=name, markersize=3)
    ax.axhline(0, color="grey", linewidth=1)
    ax.set_xlabel("Es/N0 [dB]")
    ax.set_ylabel("Estimate - true [dB]")
    ax.set_title("Es/N0 estimator bias")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out / "esn0_estimate.png")
    plt.close(fig)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("csv", type=Path)
    p.add_argument("--out", type=Path, help="directory for the figures and summary.md")
    p.add_argument("--check", action="store_true", help="fail if a threshold is below the need")
    args = p.parse_args()

    data = load(args.csv)
    limits = thresholds()
    table, ok = summary(data, limits)
    print(table)
    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / "summary.md").write_text(table + "\n", encoding="utf-8")
        plot(data, limits, args.out)
    if args.check and not ok:
        print("FAIL: a MODCOD threshold is below the Es/N0 the modem needs", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
