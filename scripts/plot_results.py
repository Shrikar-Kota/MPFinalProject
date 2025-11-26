#!/usr/bin/env python3
"""
Skip List Benchmark Results Plotting Script
Handles duplicate entries by taking the mean
"""

import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import sys
import os

sns.set_style("whitegrid")
sns.set_palette("husl")

def plot_scalability(df, output_dir):
    """Plot throughput vs thread count"""
    # Filter for scalability data (mixed workload, various thread counts)
    scalability_df = df[df['workload'] == 'mixed'].copy()
    
    # Remove duplicates by taking mean
    scalability_df = scalability_df.groupby(['impl', 'threads']).agg({
        'throughput': 'mean'
    }).reset_index()
    
    plt.figure(figsize=(10, 6))
    
    for impl in scalability_df['impl'].unique():
        impl_data = scalability_df[scalability_df['impl'] == impl]
        plt.plot(impl_data['threads'], impl_data['throughput'] / 1e6, 
                marker='o', linewidth=2, markersize=8, label=impl)
    
    plt.xlabel('Number of Threads', fontsize=12)
    plt.ylabel('Throughput (M ops/sec)', fontsize=12)
    plt.title('Scalability: Throughput vs Thread Count', fontsize=14, fontweight='bold')
    plt.legend(fontsize=11)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'scalability.png')
    plt.savefig(output_path, dpi=300)
    plt.close()
    print(f"Saved: {output_path}")

def plot_speedup(df, output_dir):
    """Plot speedup relative to single thread"""
    scalability_df = df[df['workload'] == 'mixed'].copy()
    
    # Remove duplicates
    scalability_df = scalability_df.groupby(['impl', 'threads']).agg({
        'throughput': 'mean'
    }).reset_index()
    
    plt.figure(figsize=(10, 6))
    
    for impl in scalability_df['impl'].unique():
        impl_data = scalability_df[scalability_df['impl'] == impl].sort_values('threads')
        baseline = impl_data[impl_data['threads'] == 1]['throughput'].values[0]
        speedup = impl_data['throughput'] / baseline
        
        plt.plot(impl_data['threads'], speedup, 
                marker='o', linewidth=2, markersize=8, label=impl)
    
    # Ideal speedup line
    max_threads = scalability_df['threads'].max()
    plt.plot([1, max_threads], [1, max_threads], 
            'k--', linewidth=1, alpha=0.5, label='Ideal')
    
    plt.xlabel('Number of Threads', fontsize=12)
    plt.ylabel('Speedup (vs 1 thread)', fontsize=12)
    plt.title('Speedup Analysis', fontsize=14, fontweight='bold')
    plt.legend(fontsize=11)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'speedup.png')
    plt.savefig(output_path, dpi=300)
    plt.close()
    print(f"Saved: {output_path}")

def plot_workload_comparison(df, output_dir):
    """Plot performance across different workloads"""
    # Filter for workload comparison (fixed thread count)
    if 'workload' not in df.columns or df['workload'].nunique() <= 1:
        print("Skipping workload comparison (insufficient data)")
        return
    
    workload_df = df[df['workload'].isin(['insert', 'readonly', 'mixed', 'delete'])].copy()
    
    if workload_df.empty:
        print("Skipping workload comparison (no data)")
        return
    
    # Remove duplicates
    workload_df = workload_df.groupby(['impl', 'workload']).agg({
        'throughput': 'mean'
    }).reset_index()
    
    # Pivot for grouped bar chart
    pivot_df = workload_df.pivot(index='workload', columns='impl', values='throughput') / 1e6
    
    plt.figure(figsize=(10, 6))
    pivot_df.plot(kind='bar', width=0.8)
    
    plt.xlabel('Workload Type', fontsize=12)
    plt.ylabel('Throughput (M ops/sec)', fontsize=12)
    plt.title('Performance Across Workloads', fontsize=14, fontweight='bold')
    plt.legend(title='Implementation', fontsize=11)
    plt.xticks(rotation=45)
    plt.grid(True, alpha=0.3, axis='y')
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'workload_comparison.png')
    plt.savefig(output_path, dpi=300)
    plt.close()
    print(f"Saved: {output_path}")

def plot_contention(df, output_dir):
    """Plot performance under different contention levels"""
    if 'key_range' not in df.columns or df['key_range'].nunique() <= 1:
        print("Skipping contention analysis (insufficient data)")
        return
    
    contention_df = df[df['key_range'].isin([1000, 10000, 100000, 1000000])].copy()
    
    if contention_df.empty:
        print("Skipping contention analysis (no data)")
        return
    
    # Remove duplicates
    contention_df = contention_df.groupby(['impl', 'key_range']).agg({
        'throughput': 'mean'
    }).reset_index()
    
    plt.figure(figsize=(10, 6))
    
    for impl in contention_df['impl'].unique():
        impl_data = contention_df[contention_df['impl'] == impl].sort_values('key_range')
        plt.plot(impl_data['key_range'], impl_data['throughput'] / 1e6,
                marker='o', linewidth=2, markersize=8, label=impl)
    
    plt.xlabel('Key Range (contention decreases →)', fontsize=12)
    plt.ylabel('Throughput (M ops/sec)', fontsize=12)
    plt.title('Performance Under Different Contention Levels', fontsize=14, fontweight='bold')
    plt.xscale('log')
    plt.legend(fontsize=11)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'contention.png')
    plt.savefig(output_path, dpi=300)
    plt.close()
    print(f"Saved: {output_path}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 plot_results.py <results.csv>")
        sys.exit(1)
    
    csv_file = sys.argv[1]
    
    if not os.path.exists(csv_file):
        print(f"Error: File '{csv_file}' not found")
        sys.exit(1)
    
    # Read CSV
    df = pd.read_csv(csv_file)
    
    # Create output directory
    output_dir = os.path.join(os.path.dirname(csv_file), 'plots')
    os.makedirs(output_dir, exist_ok=True)
    
    print("Generating plots...")
    
    # Generate all plots
    plot_scalability(df, output_dir)
    plot_speedup(df, output_dir)
    plot_workload_comparison(df, output_dir)
    plot_contention(df, output_dir)
    
    print(f"\nAll plots saved to: {output_dir}/")
    print("\nGenerated plots:")
    print("  - scalability.png: Throughput vs thread count")
    print("  - speedup.png: Speedup relative to 1 thread")
    print("  - workload_comparison.png: Performance across workloads")
    print("  - contention.png: Performance under contention")

if __name__ == '__main__':
    main()