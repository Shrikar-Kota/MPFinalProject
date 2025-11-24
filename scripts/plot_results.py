#!/usr/bin/env python3

import sys
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
from pathlib import Path

def load_results(csv_file):
    df = pd.read_csv(csv_file)
    print(f"Loaded {len(df)} benchmark results from {csv_file}")
    return df

def plot_scalability(df, output_dir):
    mixed_df = df[df['workload'] == 'mixed'].copy()
    if mixed_df.empty:
        return
    
    plt.figure(figsize=(10, 6))
    for impl in mixed_df['impl'].unique():
        impl_data = mixed_df[mixed_df['impl'] == impl].sort_values('threads')
        plt.plot(impl_data['threads'], impl_data['throughput'] / 1e6, 
                marker='o', linewidth=2, markersize=8, label=impl.capitalize())
    
    plt.xlabel('Number of Threads', fontsize=12)
    plt.ylabel('Throughput (Million ops/sec)', fontsize=12)
    plt.title('Scalability: Throughput vs Thread Count', fontsize=14, fontweight='bold')
    plt.legend(fontsize=11)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(output_dir / 'scalability.png', dpi=300, bbox_inches='tight')
    print(f"Saved: {output_dir / 'scalability.png'}")
    plt.close()

def plot_speedup(df, output_dir):
    mixed_df = df[df['workload'] == 'mixed'].copy()
    if mixed_df.empty:
        return
    
    plt.figure(figsize=(10, 6))
    for impl in mixed_df['impl'].unique():
        impl_data = mixed_df[mixed_df['impl'] == impl].sort_values('threads')
        baseline = impl_data[impl_data['threads'] == 1]['throughput'].values
        if len(baseline) == 0:
            continue
        impl_data['speedup'] = impl_data['throughput'] / baseline[0]
        plt.plot(impl_data['threads'], impl_data['speedup'], 
                marker='o', linewidth=2, markersize=8, label=impl.capitalize())
    
    max_threads = mixed_df['threads'].max()
    plt.plot([1, max_threads], [1, max_threads], 'k--', linewidth=1.5, alpha=0.5, label='Ideal')
    
    plt.xlabel('Number of Threads', fontsize=12)
    plt.ylabel('Speedup', fontsize=12)
    plt.title('Speedup Relative to Single Thread', fontsize=14, fontweight='bold')
    plt.legend(fontsize=11)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(output_dir / 'speedup.png', dpi=300, bbox_inches='tight')
    print(f"Saved: {output_dir / 'speedup.png'}")
    plt.close()

def plot_workload_comparison(df, output_dir):
    thread_count = 8
    workload_df = df[df['threads'] == thread_count].copy()
    if workload_df.empty:
        return
    
    pivot_df = workload_df.pivot(index='workload', columns='impl', values='throughput') / 1e6
    ax = pivot_df.plot(kind='bar', figsize=(10, 6), width=0.8)
    
    plt.xlabel('Workload Type', fontsize=12)
    plt.ylabel('Throughput (Million ops/sec)', fontsize=12)
    plt.title(f'Workload Comparison ({thread_count} Threads)', fontsize=14, fontweight='bold')
    plt.legend(title='Implementation', fontsize=11)
    plt.xticks(rotation=0)
    plt.grid(True, alpha=0.3, axis='y')
    plt.tight_layout()
    plt.savefig(output_dir / 'workload_comparison.png', dpi=300, bbox_inches='tight')
    print(f"Saved: {output_dir / 'workload_comparison.png'}")
    plt.close()

def generate_summary(df, output_dir):
    summary = df.groupby('impl').agg({
        'throughput': ['mean', 'std', 'max'],
        'time': ['mean', 'min']
    }).round(2)
    
    summary.columns = ['_'.join(col) for col in summary.columns.values]
    summary = summary.reset_index()
    
    for col in summary.columns:
        if 'throughput' in col:
            summary[col] = summary[col] / 1e6
    
    print("\n=== Summary Statistics ===")
    print(summary.to_string(index=False))
    
    summary.to_csv(output_dir / 'summary_statistics.csv', index=False)
    print(f"\nSaved: {output_dir / 'summary_statistics.csv'}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 plot_results.py <results.csv>")
        sys.exit(1)
    
    csv_file = sys.argv[1]
    df = load_results(csv_file)
    
    output_dir = Path('results') / 'plots'
    output_dir.mkdir(parents=True, exist_ok=True)
    
    sns.set_style("whitegrid")
    
    print("\nGenerating plots...")
    plot_scalability(df, output_dir)
    plot_speedup(df, output_dir)
    plot_workload_comparison(df, output_dir)
    generate_summary(df, output_dir)
    
    print(f"\nAll plots saved to: {output_dir}")
    print("Done!")

if __name__ == "__main__":
    main()