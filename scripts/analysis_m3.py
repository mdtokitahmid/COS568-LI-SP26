#!/usr/bin/env python3
"""
Milestone 3 — 3 portrait SVGs (one per dataset).
Each SVG has 4 stacked horizontal bar panels:
  Throughput 10% | Index Size 10% | Throughput 90% | Index Size 90%
Data: CSV files, last occurrence per (index, variant) combo.

Run:  python scripts/analysis_m3.py
"""

import csv, html, os
from typing import Dict, List, Optional, Tuple
try:
    import cairosvg
    _HAVE_CAIRO = True
except ImportError:
    _HAVE_CAIRO = False

RESULTS_DIR = "results"
OUTPUT_DIR  = "analysis_results"

DATASETS = [
    "fb_100M_public_uint64",
    "books_100M_public_uint64",
    "osmc_100M_public_uint64",
]
DATASET_LABELS = {
    "fb_100M_public_uint64":    "Facebook (FB)",
    "books_100M_public_uint64": "Books",
    "osmc_100M_public_uint64":  "OSMC",
}
WORKLOAD_SUFFIX = {
    "10pct": "ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix",
    "90pct": "ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix",
}
INDEXES = ["DynamicPGM", "LIPP", "HybridPGMLIPP", "ARIA"]
COLORS  = {
    "DynamicPGM":         "#4C72B0",
    "LIPP":               "#DD8452",
    "HybridPGMLIPP":      "#55A868",
    "ARIA": "#C44E52",
}
CSV_COLS = [
    "index_name", "build_time_ns1", "build_time_ns2", "build_time_ns3",
    "index_size_bytes", "mixed_throughput_mops1", "mixed_throughput_mops2",
    "mixed_throughput_mops3", "search_method", "value", "policy",
]

Row = Dict[str, object]


def to_float(v, default=0.0):
    try:
        return float(v) if v not in (None, "") else default
    except (TypeError, ValueError):
        return default


def load_csv(path: str, workload: str) -> List[Row]:
    raw: List[Row] = []
    with open(path, newline="") as f:
        for line in csv.reader(f):
            if not line or line[0].strip() == "index_name":
                continue
            if len(line) < 8:
                continue
            row: Row = {"workload": workload}
            for i, col in enumerate(CSV_COLS):
                row[col] = line[i].strip() if i < len(line) else ""
            for col in CSV_COLS[1:8]:
                row[col] = to_float(row[col])
            raw.append(row)

    # Last occurrence per (index, variant) wins
    seen: Dict[tuple, Row] = {}
    for row in raw:
        key = (row["index_name"],
               str(row.get("search_method", "")),
               str(row.get("value", "")),
               str(row.get("policy", "")))
        seen[key] = row
    return list(seen.values())


def load_dataset(dataset: str) -> Dict[str, List[Row]]:
    out = {}
    for tag, suffix in WORKLOAD_SUFFIX.items():
        path = os.path.join(RESULTS_DIR, f"{dataset}_{suffix}_results_table.csv")
        out[tag] = load_csv(path, tag) if os.path.exists(path) else []
    return out


def mean_tput(row: Row) -> float:
    return (to_float(row["mixed_throughput_mops1"])
          + to_float(row["mixed_throughput_mops2"])
          + to_float(row["mixed_throughput_mops3"])) / 3.0


def best_row(rows: List[Row], index_name: str) -> Optional[Row]:
    cands = [r for r in rows if r["index_name"] == index_name]
    return max(cands, key=mean_tput) if cands else None


def hyperparam(index_name: str, row: Optional[Row]) -> str:
    if row is None:
        return ""
    sm  = str(row.get("search_method", "")).strip()
    val = str(row.get("value",         "")).strip()
    pol = str(row.get("policy",        "")).strip()

    def fmt_num(s: str) -> str:
        try:
            n = int(float(s))
            if n >= 1_000_000: return f"{n // 1_000_000}M"
            if n >= 1_000:     return f"{n // 1_000}K"
            return str(n)
        except (ValueError, TypeError):
            return s

    if index_name == "DynamicPGM":
        parts = ([sm] if sm else []) + ([f"ε={val}"] if val else [])
        return ", ".join(parts)
    if index_name == "LIPP":
        return ""
    if index_name == "HybridPGMLIPP":
        parts = ([f"ε={sm}"] if sm else []) + ([f"p={val}"] if val else [])
        return ", ".join(parts)
    if index_name == "ARIA":
        parts = (([f"ε={sm}"] if sm else [])
                 + ([f"flush={fmt_num(val)}"] if val else [])
                 + ([pol] if pol else []))
        return ", ".join(parts)
    return ""


def extract(rows: List[Row]) -> Tuple[Dict, Dict, Dict]:
    tput, size, hyper = {}, {}, {}
    for idx in INDEXES:
        row = best_row(rows, idx)
        tput[idx]  = mean_tput(row) if row else 0.0
        size[idx]  = to_float(row["index_size_bytes"]) / 1e9 if row else 0.0
        hyper[idx] = hyperparam(idx, row)
    return tput, size, hyper


# ── SVG helpers ───────────────────────────────────────────────────────────────

def T(x, y, text, sz=10, anchor="middle", bold=False, color="#222"):
    fw = "bold" if bold else "normal"
    return (f'<text x="{x:.1f}" y="{y:.1f}" font-family="Arial,sans-serif" '
            f'font-size="{sz}" font-weight="{fw}" fill="{color}" '
            f'text-anchor="{anchor}">{html.escape(str(text))}</text>')


def hpanel(x0: float, y0: float, pw: float, ph: float,
           title: str, values: Dict[str, float], xlabel: str,
           hyperparams: Optional[Dict[str, str]] = None) -> List[str]:
    """One horizontal-bar panel at pixel position (x0,y0) size (pw×ph)."""
    out: List[str] = []

    # Panel background + border
    out.append(f'<rect x="{x0:.1f}" y="{y0:.1f}" width="{pw:.1f}" height="{ph:.1f}" '
               f'fill="#f7f9fc" rx="5" stroke="#b0b8c8" stroke-width="1"/>')

    # Title
    out.append(T(x0 + pw / 2, y0 + 20, title, sz=12, bold=True))

    pad_l = 148   # index name labels
    pad_r = 58    # value labels
    pad_t = 32    # below title
    pad_b = 34    # x-axis ticks + label

    bx = x0 + pad_l
    by = y0 + pad_t
    bw = pw - pad_l - pad_r
    bh = ph - pad_t - pad_b

    n     = len(INDEXES)
    slot  = bh / n
    bar_h = slot * 0.52

    vmax  = max((values.get(i, 0.0) for i in INDEXES), default=1.0)
    vmax  = (vmax * 1.20) if vmax > 0 else 1.0
    scale = bw / vmax

    # Grid lines + tick labels
    for k in range(5):
        vt = vmax * k / 4
        xt = bx + vt * scale
        out.append(f'<line x1="{xt:.1f}" y1="{by:.1f}" x2="{xt:.1f}" '
                   f'y2="{by+bh:.1f}" stroke="#dde3ec" stroke-width="1"/>')
        out.append(T(xt, by + bh + 15, f"{vt:.1f}", sz=9))

    # Axis lines
    out.append(f'<line x1="{bx:.1f}" y1="{by:.1f}" x2="{bx:.1f}" '
               f'y2="{by+bh:.1f}" stroke="#888" stroke-width="1"/>')
    out.append(f'<line x1="{bx:.1f}" y1="{by+bh:.1f}" x2="{bx+bw:.1f}" '
               f'y2="{by+bh:.1f}" stroke="#888" stroke-width="1"/>')

    # x-axis label
    out.append(T(bx + bw / 2, y0 + ph - 8, xlabel, sz=10, bold=True, color="#555"))

    # Bars + labels
    for i, idx in enumerate(INDEXES):
        val   = values.get(idx, 0.0)
        bar_y = by + i * slot + (slot - bar_h) / 2
        bar_w = max(val * scale, 0)
        hp    = (hyperparams or {}).get(idx, "")

        # Subtle bar background track
        out.append(f'<rect x="{bx:.1f}" y="{bar_y:.1f}" width="{bw:.1f}" '
                   f'height="{bar_h:.1f}" fill="#e8ecf2" rx="2"/>')
        # Colored bar
        out.append(f'<rect x="{bx:.1f}" y="{bar_y:.1f}" width="{bar_w:.2f}" '
                   f'height="{bar_h:.1f}" fill="{COLORS[idx]}" opacity="0.90" rx="2"/>')

        # Index name + optional hyperparam sub-label (left)
        if hp:
            name_y = bar_y + bar_h / 2
            out.append(T(bx - 7, name_y,      idx, sz=9.5, anchor="end"))
            out.append(T(bx - 7, name_y + 11, hp,  sz=8,   anchor="end", color="#777"))
        else:
            mid_y = bar_y + bar_h / 2 + 4
            out.append(T(bx - 7, mid_y, idx, sz=10, anchor="end"))

        # Value label (right of bar)
        lbl   = f"{val:.2f}" if val > 0 else "—"
        mid_y = bar_y + bar_h / 2 + 4
        out.append(T(bx + bar_w + 5, mid_y, lbl, sz=9.5, anchor="start", bold=True))

    return out


def write_dataset_png(path: str, dataset_label: str,
                      tput10: Dict, size10: Dict, hyper10: Dict,
                      tput90: Dict, size90: Dict, hyper90: Dict) -> None:
    """2x2 grid PNG — dataset name as sole title, no legend, no footer."""

    PAD_X   = 30
    PAD_TOP = 42    # dataset name only
    PAD_BOT = 14    # small bottom margin
    GAP_X   = 20
    GAP_Y   = 20

    SVG_W   = 800
    panel_w = (SVG_W - 2 * PAD_X - GAP_X) // 2

    SVG_H   = 800
    panel_h = (SVG_H - PAD_TOP - PAD_BOT - GAP_Y) // 2

    grid = [
        [
            (tput10, hyper10, "Throughput — 10% Insert (90% Lookup)", "Mops/s"),
            (tput90, hyper90, "Throughput — 90% Insert (10% Lookup)", "Mops/s"),
        ],
        [
            (size10, hyper10, "Index Size — 10% Insert (90% Lookup)", "GB"),
            (size90, hyper90, "Index Size — 90% Insert (10% Lookup)", "GB"),
        ],
    ]

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" '
        f'width="{SVG_W}" height="{SVG_H}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        T(SVG_W / 2, 26, dataset_label, sz=15, bold=True),
        f'<line x1="{PAD_X}" y1="34" x2="{SVG_W-PAD_X}" y2="34" '
        f'stroke="#cccccc" stroke-width="0.8"/>',
    ]

    for r, row_panels in enumerate(grid):
        for c, (vals, hypers, title, xlabel) in enumerate(row_panels):
            x0 = PAD_X + c * (panel_w + GAP_X)
            y0 = PAD_TOP + r * (panel_h + GAP_Y)
            parts.extend(hpanel(x0, y0, panel_w, panel_h, title, vals, xlabel, hypers))

    parts.append("</svg>")
    svg_str = "\n".join(parts)

    if _HAVE_CAIRO:
        cairosvg.svg2png(bytestring=svg_str.encode(),
                         write_to=path, scale=3.0)
    else:
        # Fallback: write SVG so the user can convert manually
        svg_path = path.replace(".png", ".svg")
        with open(svg_path, "w") as f:
            f.write(svg_str)
        print(f"  cairosvg not found — saved SVG to {svg_path}")
        print("  Install with: pip install cairosvg")


def print_summary(dataset_label, tput10, size10, tput90, size90):
    print(f"\n{'═'*54}")
    print(f"  {dataset_label}")
    for wl_label, tput, size in [
        ("10% Insert (90% Lookup)", tput10, size10),
        ("90% Insert (10% Lookup)", tput90, size90),
    ]:
        print(f"  {'─'*50}")
        print(f"  {wl_label}")
        print(f"  {'Index':<22} {'Mops/s':>9} {'Size GB':>9}")
        best = max(tput.values()) if tput else 0
        for idx in INDEXES:
            flag = " ★" if tput.get(idx, 0) == best else ""
            print(f"  {idx:<22} {tput.get(idx,0):>9.3f} {size.get(idx,0):>9.2f}{flag}")


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    summary_rows = []

    for dataset in DATASETS:
        label = DATASET_LABELS[dataset]
        rows  = load_dataset(dataset)

        tput10, size10, hyper10 = extract(rows["10pct"])
        tput90, size90, hyper90 = extract(rows["90pct"])

        print_summary(label, tput10, size10, tput90, size90)

        ds_short  = dataset.split("_")[0]   # fb / books / osmc
        png_path  = os.path.join(OUTPUT_DIR, f"milestone3_{ds_short}.png")
        write_dataset_png(png_path, label, tput10, size10, hyper10, tput90, size90, hyper90)
        print(f"  → {png_path}")

        for wl, tput, size in [("10pct", tput10, size10), ("90pct", tput90, size90)]:
            for idx in INDEXES:
                summary_rows.append([label, wl, idx,
                                     f"{tput.get(idx,0):.4f}",
                                     f"{size.get(idx,0):.3f}"])

    summary_path = os.path.join(OUTPUT_DIR, "milestone3_summary.csv")
    with open(summary_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["dataset", "workload", "index", "throughput_mops", "size_gb"])
        w.writerows(summary_rows)
    print(f"\nSummary CSV → {summary_path}")


if __name__ == "__main__":
    main()
