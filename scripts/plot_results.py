import json
import glob
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import argparse
import os

def load_data(results_dir):
    data = []
    ts_data = []
    for file in glob.glob(os.path.join(results_dir, "results_*.json")):
        with open(file, 'r') as f:
            j = json.load(f)
            db_name = file.split("results_")[-1].split(".json")[0]
            if "_run" in db_name:
                db_name = db_name.split("_run")[0]
            
            row = {
                "database": db_name,
                "latency_avg": j.get("latency_stats", {}).get("avg_us", 0),
                "latency_p50": j.get("latency_stats", {}).get("p50_us", 0),
                "latency_p95": j.get("latency_stats", {}).get("p95_us", 0),
                "latency_p99": j.get("latency_stats", {}).get("p99_us", 0),
                "throughput": j.get("throughput_ops", 0),
                "write_amp": j.get("amplification", {}).get("write_amp", 0),
                "read_amp": j.get("amplification", {}).get("read_amp", 0),
                "space_amp": j.get("amplification", {}).get("space_amp", 0)
            }
            data.append(row)
            
            for pt in j.get("time_series", []):
                ts_row = {
                    "database": db_name,
                    "elapsed_time_s": round(pt.get("elapsed_time_s", 0)),
                    "throughput": pt.get("throughput_ops", 0),
                    "latency_p99": pt.get("latency_stats", {}).get("p99_us", 0)
                }
                ts_data.append(ts_row)
    return pd.DataFrame(data), pd.DataFrame(ts_data)

def load_telemetry(results_dir):
    data = []
    for file in glob.glob(os.path.join(results_dir, "telemetry_*.csv")):
        db_name = file.split("telemetry_")[-1].split(".csv")[0]
        if "_run" in db_name:
            db_name = db_name.split("_run")[0]
            
        try:
            # dstat outputs headers around line 6, but we can search for "time" or just skip 5 rows
            df = pd.read_csv(file, skiprows=5, on_bad_lines='skip')
            
            # CPU usage = 100 - idle. 'idl' is usually the 4th column in CPU stats
            if 'idl' in df.columns:
                cpu_usage = 100 - pd.to_numeric(df['idl'], errors='coerce')
            else:
                cpu_usage = 100 - pd.to_numeric(df.iloc[:, 3], errors='coerce')
                
            for i, val in enumerate(cpu_usage):
                if pd.notna(val):
                    data.append({
                        "database": db_name,
                        "elapsed_time_s": i,
                        "cpu_usage": val
                    })
        except Exception as e:
            print(f"Failed to parse {file}: {e}")
            
    return pd.DataFrame(data)

def plot_metrics(df, output_dir):
    os.makedirs(output_dir, exist_ok=True)
    
    # Prettify database names
    df['database'] = df['database'].str.title()
    
    sns.set_theme(style="whitegrid", font_scale=1.2)
    sns.set_palette("husl")

    metrics = [
        ("latency_avg", "Average Latency (us)", "Latency"),
        ("latency_p99", "P99 Latency (us)", "Tail Latency (P99)"),
        ("throughput", "Throughput (ops/s)", "Throughput"),
        ("write_amp", "Write Amplification", "Write Amplification"),
        ("space_amp", "Space Amplification", "Space Amplification")
    ]

    for col, ylabel, title in metrics:
        if col in df.columns and df[col].sum() > 0:
            plt.figure(figsize=(12, 7))
            
            # Create bar plot (Seaborn handles error bars automatically for multiple runs via capsize and errorbar parameters)
            ax = sns.barplot(data=df, x="database", y=col, hue="database", palette="husl", dodge=False, legend=False, errorbar='sd', capsize=0.1)
            
            # Add labels on top of bars
            for p in ax.patches:
                height = p.get_height()
                if height > 0:
                    ax.annotate(f'{height:,.1f}',
                                (p.get_x() + p.get_width() / 2., height),
                                ha='center', va='bottom',
                                xytext=(0, 5),
                                textcoords='offset points',
                                fontweight='bold',
                                fontsize=11)
            
            # Styling tweaks
            plt.title(f"{title} Comparison", pad=20, fontweight='bold', fontsize=16)
            plt.ylabel(ylabel, fontweight='bold')
            plt.xlabel("Database Engine", fontweight='bold')
            
            # Clean up axes
            sns.despine(left=True, bottom=True)
            plt.grid(axis='x') # Remove vertical grid lines
            
            plt.tight_layout()
            plt.savefig(os.path.join(output_dir, f"{col}.png"), dpi=300, bbox_inches='tight')
            plt.close()
            print(f"Saved {col}.png")

def plot_time_series(df_ts, output_dir):
    if df_ts.empty:
        return
        
    os.makedirs(output_dir, exist_ok=True)
    df_ts['database'] = df_ts['database'].str.title()
    sns.set_theme(style="whitegrid", font_scale=1.2)
    
    # Plot Throughput over Time
    plt.figure(figsize=(14, 6))
    sns.lineplot(data=df_ts, x="elapsed_time_s", y="throughput", hue="database", palette="husl", linewidth=2.5)
    plt.title("Throughput Over Time (Workload Shifter)", pad=20, fontweight='bold', fontsize=16)
    plt.ylabel("Throughput (ops/s)", fontweight='bold')
    plt.xlabel("Elapsed Time (s)", fontweight='bold')
    plt.axvline(x=10, color='r', linestyle='--', alpha=0.5)
    plt.axvline(x=20, color='g', linestyle='--', alpha=0.5)
    sns.despine(left=True, bottom=True)
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, "ts_throughput.png"), dpi=300, bbox_inches='tight')
    plt.close()
    print("Saved ts_throughput.png")

    # Plot P99 Latency over Time
    plt.figure(figsize=(14, 6))
    sns.lineplot(data=df_ts, x="elapsed_time_s", y="latency_p99", hue="database", palette="husl", linewidth=2.5)
    plt.title("P99 Latency Over Time (Workload Shifter)", pad=20, fontweight='bold', fontsize=16)
    plt.ylabel("P99 Latency (us)", fontweight='bold')
    plt.xlabel("Elapsed Time (s)", fontweight='bold')
    plt.axvline(x=10, color='r', linestyle='--', alpha=0.5)
    plt.axvline(x=20, color='g', linestyle='--', alpha=0.5)
    sns.despine(left=True, bottom=True)
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, "ts_latency_p99.png"), dpi=300, bbox_inches='tight')
    plt.savefig(os.path.join(output_dir, "ts_latency_p99.png"), dpi=300, bbox_inches='tight')
    plt.close()
    print("Saved ts_latency_p99.png")

def plot_telemetry(df_tel, output_dir):
    if df_tel.empty:
        return
    os.makedirs(output_dir, exist_ok=True)
    df_tel['database'] = df_tel['database'].str.title()
    sns.set_theme(style="whitegrid", font_scale=1.2)
    
    plt.figure(figsize=(14, 6))
    sns.lineplot(data=df_tel, x="elapsed_time_s", y="cpu_usage", hue="database", palette="husl", linewidth=2.5)
    plt.title("CPU Usage Over Time (Workload Shifter)", pad=20, fontweight='bold', fontsize=16)
    plt.ylabel("CPU Usage (%)", fontweight='bold')
    plt.xlabel("Elapsed Time (s)", fontweight='bold')
    plt.axvline(x=10, color='r', linestyle='--', alpha=0.5)
    plt.axvline(x=20, color='g', linestyle='--', alpha=0.5)
    sns.despine(left=True, bottom=True)
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, "ts_cpu_usage.png"), dpi=300, bbox_inches='tight')
    plt.close()
    print("Saved ts_cpu_usage.png")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Plot benchmark results")
    parser.add_argument("--results-dir", type=str, default=".", help="Directory containing JSON results")
    parser.add_argument("--output-dir", type=str, default="plots", help="Directory to save plots")
    args = parser.parse_args()

    df, df_ts = load_data(args.results_dir)
    df_tel = load_telemetry(args.results_dir)
    
    if not df.empty:
        print(f"Loaded benchmark data for {df['database'].nunique()} databases across {len(df) // df['database'].nunique()} runs.")
        plot_metrics(df, args.output_dir)
        plot_time_series(df_ts, args.output_dir)
    else:
        print("No result JSON files found.")
        
    if not df_tel.empty:
        print(f"Loaded telemetry data for {df_tel['database'].nunique()} databases.")
        plot_telemetry(df_tel, args.output_dir)
