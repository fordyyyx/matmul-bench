"""Draw the README chart from the sweep CSV.

Writes two PNGs, one for GitHub's light theme and one for its dark theme; the
README picks between them with a <picture> element.

    python python/plot.py                       # reads results/readme_sweep.csv
"""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")  # render to files; no window needed
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

# Series order = colour slot order (validated for colour-blind separation in
# both themes). The headline series takes slot 1.
SERIES = ["blocked512 (padded)", "blocked512 (unpadded)", "reordered", "naive"]

# Background and text colours match GitHub's own themes so the chart blends in.
THEMES = {
    "light": {
        "surface": "#ffffff", "text": "#1f2328", "muted": "#59636e", "grid": "#e6e8eb",
        "series": ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"],
    },
    "dark": {
        "surface": "#0d1117", "text": "#f0f6fc", "muted": "#9198a1", "grid": "#262c36",
        "series": ["#3987e5", "#d95926", "#199e70", "#c98500"],
    },
}


def load(path: Path) -> dict[str, list[tuple[int, float]]]:
    data: dict[str, list[tuple[int, float]]] = defaultdict(list)
    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            data[row["label"]].append((int(row["n"]), float(row["gflops"])))
    return {label: sorted(points) for label, points in data.items()}


def draw(data: dict[str, list[tuple[int, float]]], theme: dict, out: Path) -> None:
    fig, ax = plt.subplots(figsize=(8, 4.6), dpi=200)
    fig.patch.set_facecolor(theme["surface"])
    ax.set_facecolor(theme["surface"])

    for label, colour in zip(SERIES, theme["series"]):
        if label not in data:
            continue
        ns, gflops = zip(*data[label])
        # 2 px line; markers ringed in the surface colour so crossings stay legible.
        ax.plot(ns, gflops, color=colour, linewidth=2, solid_capstyle="round",
                marker="o", markersize=5, markeredgecolor=theme["surface"],
                markeredgewidth=1.2, label=label, zorder=3)
        # Direct label at the line's end, in text colour (never the series colour).
        ax.annotate(label, (ns[-1], gflops[-1]), xytext=(8, 0), textcoords="offset points",
                    va="center", fontsize=8.5, color=theme["text"])

    # Call out the unpadded dips at n = 2048 and 4096 (rows exactly a multiple of
    # 4 KB apart): one note in the clear band between the unpadded and reordered
    # lines, with a thin leader line out to each dip.
    dips = [(n, g) for n, g in data.get("blocked512 (unpadded)", []) if n in (2048, 4096)]
    if dips:
        ax.text(3050, 20.2, "unpadded dips: power-of-two\nrow stride → cache conflict misses",
                ha="center", va="center", fontsize=8, color=theme["muted"])
        for n, g in dips:
            # Leave from just above the note's top edge, on the side nearer the dip.
            start_x = 2700 if n < 3050 else 3400
            end_x = n + 70 if n < 3050 else n - 70
            ax.annotate("", xy=(end_x, g), xytext=(start_x, 22.6),
                        arrowprops={"arrowstyle": "-", "color": theme["muted"], "lw": 0.8})

    # Recessive axes: hairline horizontal grid, no top/right spines.
    ax.grid(axis="y", color=theme["grid"], linewidth=0.8, linestyle="-")
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(theme["grid"])
    ax.tick_params(colors=theme["muted"], labelsize=8.5, length=0)

    ax.set_xticks([256, 1024, 2048, 3072, 4096])
    ax.set_xlim(0, 4096 * 1.22)  # room on the right for the end labels
    ax.set_ylim(0, 45)
    ax.set_xlabel("Matrix size n (n × n float32)", color=theme["muted"], fontsize=9)
    ax.set_ylabel("GFLOP/s (higher is better)", color=theme["muted"], fontsize=9)
    ax.set_title("Matrix multiply throughput by kernel", color=theme["text"],
                 fontsize=11, loc="left", pad=10)

    legend = ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.16), ncol=4,
                       frameon=False, fontsize=8.5, handlelength=1.6)
    for text in legend.get_texts():
        text.set_color(theme["text"])

    fig.tight_layout()
    fig.savefig(out, facecolor=theme["surface"])
    plt.close(fig)
    print(f"wrote {out}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data", type=Path, default=ROOT / "results" / "readme_sweep.csv")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "results")
    args = parser.parse_args()

    data = load(args.data)
    for name, theme in THEMES.items():
        draw(data, theme, args.out_dir / f"gflops_vs_n_{name}.png")


if __name__ == "__main__":
    main()
