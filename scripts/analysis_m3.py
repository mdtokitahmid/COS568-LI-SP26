"""
Milestone 3 Analysis — Async Hybrid DPGM + LIPP
================================================
Generates 12 bar plots across 3 datasets × 2 workloads × 2 metrics:
  Datasets  : Facebook (fb), Books (books), OSMC (osmc)
  Workloads : 10% insert (90% lookup), 90% insert (10% lookup)
  Metrics   : Throughput (Mops/s), Index size (GB)

Output: one PNG per dataset saved to analysis_results/
  milestone3_fb.png
  milestone3_books.png
  milestone3_osmc.png

Run from the project root:
    python scripts/analysis_m3.py
"""

import os
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

RESULTS_DIR = "results"
OUTPUT_DIR  = "analysis_results"
os.makedirs(OUTPUT_DIR, exist_ok=True)

DATASETS = {
    "fb":    "fb_100M_public_uint64",
    "books": "books_100M_public_uint64",
    "osmc":  "osmc_100M_public_uint64",
}

WORKLOAD_SUFFIX = {
    "10pct": "ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix",
    "90pct": "ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix",
}

INDEXES = ["DynamicPGM", "LIPP", "HybridPGMLIPP", "HybridPGMLIPPAsync"]
COLORS  = {
    "DynamicPGM":        "#4C72B0",
    "LIPP":              "#DD8452",
    "HybridPGMLIPP":     "#55A868",
    "HybridPGMLIPPAsync":"#C44E52",
}


# ---------------------------------------------------------------------------
def best_row(df, index_name):
    """Return the row with the highest mean throughput for index_name."""
    rows = df[df["index_name"] == index_name].copy()
    if rows.empty:
        return None
    rows["mean_tput"] = rows[
        ["mixed_throughput_mops1", "mixed_throughput_mops2", "mixed_throughput_mops3"]
    ].mean(axis=1)
    return rows.loc[rows["mean_tput"].idxmax()]


def extract(df):
    """
    Returns dicts keyed by index name:
        throughput[index] = best mean throughput (Mops/s)
        size_gb[index]    = index size in GB
        hyperparam[index] = human-readable best hyperparams
    Missing indexes are filled with 0.
    """
    throughput, size_gb, hyperparam = {}, {}, {}
    for idx in INDEXES:
        row = best_row(df, idx)
        if row is None:
            throughput[idx] = 0.0
            size_gb[idx]    = 0.0
            hyperparam[idx] = "N/A"
            continue
        throughput[idx] = row["mean_tput"]
        size_gb[idx]    = row["index_size_bytes"] / 1e9
        sm  = str(row.get("search_method", "")).strip()
        val = str(row.get("value", "")).strip()
        if idx == "HybridPGMLIPPAsync":
            hyperparam[idx] = f"ε={sm}, flush={val}"
        elif idx == "HybridPGMLIPP":
            hyperparam[idx] = f"ε={sm}, flush={val}%"
        elif idx == "DynamicPGM":
            hyperparam[idx] = f"{sm}, ε={val}"
        else:
            hyperparam[idx] = ""
    return throughput, size_gb, hyperparam


def bar_plot(ax, values, title, ylabel, annotate=None):
    """Draw one bar chart with one bar per index."""
    x    = range(len(INDEXES))
    bars = ax.bar(x, [values[i] for i in INDEXES],
                  color=[COLORS[i] for i in INDEXES],
                  edgecolor="black", linewidth=0.6, width=0.5)

    for bar, idx in zip(bars, INDEXES):
        h = bar.get_height()
        if h == 0:
            continue
        label = f"{h:.2f}"
        if annotate and annotate.get(idx):
            label += f"\n({annotate[idx]})"
        ax.text(bar.get_x() + bar.get_width() / 2, h * 1.01,
                label, ha="center", va="bottom", fontsize=6.5)

    ax.set_title(title, fontsize=10, pad=6)
    ax.set_ylabel(ylabel, fontsize=9)
    ax.set_xticks(list(x))
    ax.set_xticklabels(INDEXES, fontsize=8, rotation=15, ha="right")
    ax.yaxis.set_minor_locator(ticker.AutoMinorLocator())
    max_val = max((values[i] for i in INDEXES), default=1)
    ax.set_ylim(0, max_val * 1.25 if max_val > 0 else 1)
    ax.grid(axis="y", linestyle="--", alpha=0.5)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)


# ---------------------------------------------------------------------------
def process_dataset(tag, dataset):
    """
    Load CSVs for one dataset, print summary tables, produce a 2×2 figure,
    and save it.  Returns True if at least one CSV was found.
    """
    csv_10 = os.path.join(RESULTS_DIR,
                          f"{dataset}_{WORKLOAD_SUFFIX['10pct']}_results_table.csv")
    csv_90 = os.path.join(RESULTS_DIR,
                          f"{dataset}_{WORKLOAD_SUFFIX['90pct']}_results_table.csv")

    has_10 = os.path.exists(csv_10)
    has_90 = os.path.exists(csv_90)

    if not has_10 and not has_90:
        print(f"[{tag}] No result CSVs found — skipping.")
        return False

    COLS = ["index_name","build_time_ns1","build_time_ns2","build_time_ns3",
            "index_size_bytes","mixed_throughput_mops1","mixed_throughput_mops2",
            "mixed_throughput_mops3","search_method","value"]

    def load_csv(path):
        raw = pd.read_csv(path, header=None)
        # If first row looks like a header (contains 'index_name'), skip it
        if str(raw.iloc[0, 0]).strip() == "index_name":
            raw = raw.iloc[1:].reset_index(drop=True)
        # Assign column names to as many columns as exist
        raw.columns = COLS[:len(raw.columns)]
        # Ensure numeric columns are numeric
        for col in COLS[1:8]:
            if col in raw.columns:
                raw[col] = pd.to_numeric(raw[col], errors="coerce")
        return raw

    df_10 = load_csv(csv_10) if has_10 else pd.DataFrame()
    df_90 = load_csv(csv_90) if has_90 else pd.DataFrame()

    tput_10, size_10, hyper_10 = (extract(df_10) if has_10
                                  else ({i: 0 for i in INDEXES},)*3)
    tput_90, size_90, hyper_90 = (extract(df_90) if has_90
                                  else ({i: 0 for i in INDEXES},)*3)

    # -- terminal summary ------------------------------------------------------
    print(f"\n=== {tag.upper()} — 10% insert (90% lookup) ===")
    print(f"{'Index':<22} {'Throughput (Mops/s)':>20} {'Size (GB)':>10}  Hyperparams")
    for idx in INDEXES:
        print(f"{idx:<22} {tput_10[idx]:>20.3f} {size_10[idx]:>10.2f}  {hyper_10[idx]}")

    print(f"\n=== {tag.upper()} — 90% insert (10% lookup) ===")
    print(f"{'Index':<22} {'Throughput (Mops/s)':>20} {'Size (GB)':>10}  Hyperparams")
    for idx in INDEXES:
        print(f"{idx:<22} {tput_90[idx]:>20.3f} {size_90[idx]:>10.2f}  {hyper_90[idx]}")

    # -- 2×2 bar plots ---------------------------------------------------------
    fig, axs = plt.subplots(2, 2, figsize=(11, 8))
    fig.suptitle(f"Milestone 3: Async Hybrid — {tag.upper()} Dataset",
                 fontsize=12, fontweight="bold", y=1.01)

    bar_plot(axs[0, 0], tput_10,
             "Throughput — 10% Insert (90% Lookup)",
             "Throughput (Mops/s)", annotate=hyper_10)
    bar_plot(axs[0, 1], size_10,
             "Index Size — 10% Insert (90% Lookup)",
             "Index Size (GB)")
    bar_plot(axs[1, 0], tput_90,
             "Throughput — 90% Insert (10% Lookup)",
             "Throughput (Mops/s)", annotate=hyper_90)
    bar_plot(axs[1, 1], size_90,
             "Index Size — 90% Insert (10% Lookup)",
             "Index Size (GB)")

    plt.tight_layout()
    out_path = os.path.join(OUTPUT_DIR, f"milestone3_{tag}.png")
    plt.savefig(out_path, dpi=150, bbox_inches="tight")
    print(f"\nPlot saved → {out_path}")
    plt.close(fig)
    return True


# ---------------------------------------------------------------------------
def main():
    summary_rows = []

    for tag, dataset in DATASETS.items():
        ok = process_dataset(tag, dataset)
        if ok:
            # collect for combined CSV
            for wl, suffix in WORKLOAD_SUFFIX.items():
                csv = os.path.join(RESULTS_DIR,
                                   f"{dataset}_{suffix}_results_table.csv")
                if not os.path.exists(csv):
                    continue
                df = pd.read_csv(csv)
                tput, size, hyper = extract(df)
                for idx in INDEXES:
                    summary_rows.append({
                        "dataset":    tag,
                        "workload":   wl,
                        "index":      idx,
                        "throughput": round(tput[idx], 4),
                        "size_gb":    round(size[idx], 3),
                        "hyperparams": hyper[idx],
                    })

    if summary_rows:
        csv_out = os.path.join(OUTPUT_DIR, "milestone3_summary.csv")
        pd.DataFrame(summary_rows).to_csv(csv_out, index=False)
        print(f"\nCombined summary CSV → {csv_out}")


if __name__ == "__main__":
    main()
