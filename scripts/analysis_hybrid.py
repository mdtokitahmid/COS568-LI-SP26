"""
Milestone 2 Analysis — Hybrid DPGM + LIPP
==========================================
Generates 4 bar plots for the Facebook dataset:
  1. Throughput — 10% insert (90% lookup) mixed workload
  2. Index size  — 10% insert (90% lookup) mixed workload
  3. Throughput — 90% insert (10% lookup) mixed workload
  4. Index size  — 90% insert (10% lookup) mixed workload

Each plot compares DynamicPGM, LIPP, and HybridPGMLIPP.
For indexes with hyperparameters, the best-performing variant is selected
(highest mean throughput averaged across all 3 repeats).

Run from the project root:
    python scripts/analysis_hybrid.py
"""

import os
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

RESULTS_DIR = "results"
OUTPUT_DIR  = "analysis_results"
os.makedirs(OUTPUT_DIR, exist_ok=True)

DATASET   = "fb_100M_public_uint64"
MIX_10PCT = f"{DATASET}_ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix_results_table.csv"
MIX_90PCT = f"{DATASET}_ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix_results_table.csv"

INDEXES = ["DynamicPGM", "LIPP", "HybridPGMLIPP", "ARIA"]
COLORS  = {
    "DynamicPGM":        "#4C72B0",
    "LIPP":              "#DD8452",
    "HybridPGMLIPP":     "#55A868",
    "ARIA":"#C44E52",
}


def best_row(df, index_name):
    """
    From all rows belonging to index_name, return the single row with the
    highest mean throughput across the 3 repeat columns.
    """
    rows = df[df["index_name"] == index_name].copy()
    if rows.empty:
        return None
    rows["mean_tput"] = rows[
        ["mixed_throughput_mops1", "mixed_throughput_mops2", "mixed_throughput_mops3"]
    ].mean(axis=1)
    return rows.loc[rows["mean_tput"].idxmax()]


def extract(df):
    """
    Returns two dicts keyed by index name:
        throughput[index] = best mean throughput (Mops/s)
        size_gb[index]    = index size in GB
    Also returns a label dict with the winning hyperparams (for annotation).
    """
    throughput = {}
    size_gb    = {}
    hyperparam = {}

    for idx in INDEXES:
        row = best_row(df, idx)
        if row is None:
            throughput[idx] = 0
            size_gb[idx]    = 0
            hyperparam[idx] = ""
            continue

        throughput[idx] = row["mean_tput"]
        size_gb[idx]    = row["index_size_bytes"] / 1e9

        # Build a human-readable hyperparameter label
        sm  = str(row.get("search_method", "")).strip()
        val = str(row.get("value", "")).strip()
        if idx == "HybridPGMLIPP":
            hyperparam[idx] = f"ε={sm}, flush={val}%"
        elif idx == "DynamicPGM":
            hyperparam[idx] = f"{sm}, ε={val}"
        else:
            hyperparam[idx] = ""

    return throughput, size_gb, hyperparam


def bar_plot(ax, values, title, ylabel, colors, annotate=None):
    """Draw a simple grouped bar chart with one bar per index."""
    x     = range(len(INDEXES))
    bars  = ax.bar(x, [values[i] for i in INDEXES],
                   color=[colors[i] for i in INDEXES],
                   edgecolor="black", linewidth=0.6, width=0.5)

    # Value labels on top of each bar
    for bar, idx in zip(bars, INDEXES):
        height = bar.get_height()
        label  = f"{height:.2f}"
        if annotate and annotate.get(idx):
            label += f"\n({annotate[idx]})"
        ax.text(bar.get_x() + bar.get_width() / 2, height * 1.01,
                label, ha="center", va="bottom", fontsize=7.5)

    ax.set_title(title, fontsize=11, pad=8)
    ax.set_ylabel(ylabel, fontsize=10)
    ax.set_xticks(list(x))
    ax.set_xticklabels(INDEXES, fontsize=10)
    ax.yaxis.set_minor_locator(ticker.AutoMinorLocator())
    ax.set_ylim(0, max(values[i] for i in INDEXES) * 1.22)
    ax.grid(axis="y", linestyle="--", alpha=0.5)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)


def main():
    path_10 = os.path.join(RESULTS_DIR, MIX_10PCT)
    path_90 = os.path.join(RESULTS_DIR, MIX_90PCT)

    if not os.path.exists(path_10) or not os.path.exists(path_90):
        print("ERROR: result CSV files not found. Run the benchmark first.")
        return

    df_10 = pd.read_csv(path_10)
    df_90 = pd.read_csv(path_90)

    tput_10, size_10, hyper_10 = extract(df_10)
    tput_90, size_90, hyper_90 = extract(df_90)

    # ------------------------------------------------------------------ #
    # Print summary table to terminal                                      #
    # ------------------------------------------------------------------ #
    print("\n=== Facebook — 10% insert (90% lookup) ===")
    print(f"{'Index':<18} {'Throughput (Mops/s)':>20} {'Size (GB)':>12}  Best hyperparams")
    for idx in INDEXES:
        print(f"{idx:<18} {tput_10[idx]:>20.3f} {size_10[idx]:>12.2f}  {hyper_10[idx]}")

    print("\n=== Facebook — 90% insert (10% lookup) ===")
    print(f"{'Index':<18} {'Throughput (Mops/s)':>20} {'Size (GB)':>12}  Best hyperparams")
    for idx in INDEXES:
        print(f"{idx:<18} {tput_90[idx]:>20.3f} {size_90[idx]:>12.2f}  {hyper_90[idx]}")

    # ------------------------------------------------------------------ #
    # 4 bar plots                                                          #
    # ------------------------------------------------------------------ #
    fig, axs = plt.subplots(2, 2, figsize=(12, 9))
    fig.suptitle("Milestone 2: Hybrid DPGM + LIPP — Facebook Dataset",
                 fontsize=13, fontweight="bold", y=1.01)

    bar_plot(axs[0, 0], tput_10,
             title="Throughput — 10% Insert (90% Lookup)",
             ylabel="Throughput (Mops/s)",
             colors=COLORS, annotate=hyper_10)

    bar_plot(axs[0, 1], size_10,
             title="Index Size — 10% Insert (90% Lookup)",
             ylabel="Index Size (GB)",
             colors=COLORS)

    bar_plot(axs[1, 0], tput_90,
             title="Throughput — 90% Insert (10% Lookup)",
             ylabel="Throughput (Mops/s)",
             colors=COLORS, annotate=hyper_90)

    bar_plot(axs[1, 1], size_90,
             title="Index Size — 90% Insert (10% Lookup)",
             ylabel="Index Size (GB)",
             colors=COLORS)

    plt.tight_layout()

    out_path = os.path.join(OUTPUT_DIR, "milestone2_hybrid_results.png")
    plt.savefig(out_path, dpi=150, bbox_inches="tight")
    print(f"\nPlot saved to {out_path}")
    plt.show()

    # ------------------------------------------------------------------ #
    # Save clean summary CSV                                               #
    # ------------------------------------------------------------------ #
    rows = []
    for idx in INDEXES:
        rows.append({
            "index":       idx,
            "workload":    "10pct_insert",
            "throughput":  round(tput_10[idx], 4),
            "size_gb":     round(size_10[idx], 3),
            "hyperparams": hyper_10[idx],
        })
        rows.append({
            "index":       idx,
            "workload":    "90pct_insert",
            "throughput":  round(tput_90[idx], 4),
            "size_gb":     round(size_90[idx], 3),
            "hyperparams": hyper_90[idx],
        })
    summary = pd.DataFrame(rows)
    csv_path = os.path.join(OUTPUT_DIR, "milestone2_summary.csv")
    summary.to_csv(csv_path, index=False)
    print(f"Summary CSV saved to {csv_path}")


if __name__ == "__main__":
    main()
