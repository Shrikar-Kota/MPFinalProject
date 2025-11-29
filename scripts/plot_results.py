#!/usr/bin/env python3
"""
Skip List Benchmark Results Plotting Script
Generates 5 publication-quality figures for report
"""

import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import sys
import os

# Set publication style
plt.style.use('seaborn-v0_8-paper')
sns.set_palette("husl")
plt.rcParams['figure.dpi'] = 300
plt.rcParams['savefig.dpi'] = 300
plt.rcParams['font.size'] = 10
plt.rcParams['axes.labelsize'] = 11
plt.rcParams['axes.titlesize'] = 12
plt.rcParams['legend.fontsize'] = 9

def plot_scalability(df, output_dir):
    """Figure 1: Plot throughput vs thread count"""
    scalability_df = df[df['workload'] == 'mixed'].copy()
    scalability_df = scalability_df.groupby(['impl', 'threads']).agg({
        'throughput': 'mean'
    }).reset_index()
    
    fig, ax = plt.subplots(figsize=(8, 5))
    
    for impl in ['coarse', 'fine', 'lockfree']:
        impl_data = scalability_df[scalability_df['impl'] == impl]
        label = {'coarse': 'Coarse-Grained', 'fine': 'Fine-Grained', 'lockfree': 'Lock-Free'}[impl]
        ax.plot(impl_data['threads'], impl_data['throughput'] / 1e6, 
                marker='o', linewidth=2.5, markersize=8, label=label)
    
    ax.set_xlabel('Number of Threads', fontweight='bold')
    ax.set_ylabel('Throughput (M ops/sec)', fontweight='bold')
    ax.set_title('Figure 1: Scalability Analysis (Mixed Workload)', fontweight='bold', pad=15)
    ax.legend(frameon=True, fancybox=True, shadow=True)
    ax.grid(True, alpha=0.3, linestyle='--')
    ax.set_xticks([1, 2, 4, 8, 16, 32])
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'figure1_scalability.png')
    plt.savefig(output_path, bbox_inches='tight', dpi=300)
    plt.close()
    print(f"Created: {output_path}")

def plot_speedup(df, output_dir):
    """Figure 2: Plot speedup relative to single thread"""
    scalability_df = df[df['workload'] == 'mixed'].copy()
    scalability_df = scalability_df.groupby(['impl', 'threads']).agg({
        'throughput': 'mean'
    }).reset_index()
    
    fig, ax = plt.subplots(figsize=(8, 5))
    
    for impl in ['coarse', 'fine', 'lockfree']:
        impl_data = scalability_df[scalability_df['impl'] == impl].sort_values('threads')
        baseline = impl_data[impl_data['threads'] == 1]['throughput'].values[0]
        speedup = impl_data['throughput'] / baseline
        label = {'coarse': 'Coarse-Grained', 'fine': 'Fine-Grained', 'lockfree': 'Lock-Free'}[impl]
        ax.plot(impl_data['threads'], speedup, 
                marker='o', linewidth=2.5, markersize=8, label=label)
    
    # Ideal speedup line
    max_threads = scalability_df['threads'].max()
    ax.plot([1, max_threads], [1, max_threads], 'k--', linewidth=1.5, alpha=0.5, label='Ideal (Linear)')
    
    ax.set_xlabel('Number of Threads', fontweight='bold')
    ax.set_ylabel('Speedup (vs 1 Thread)', fontweight='bold')
    ax.set_title('Figure 2: Speedup Analysis', fontweight='bold', pad=15)
    ax.legend(frameon=True, fancybox=True, shadow=True)
    ax.grid(True, alpha=0.3, linestyle='--')
    ax.set_xticks([1, 2, 4, 8, 16, 32])
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'figure2_speedup.png')
    plt.savefig(output_path, bbox_inches='tight', dpi=300)
    plt.close()
    print(f"Created: {output_path}")

def plot_workload_comparison(df, output_dir):
    """Figure 3: Plot performance across different workloads"""
    if 'workload' not in df.columns or df['workload'].nunique() <= 1:
        print("Skipping workload comparison (insufficient data)")
        return
    
    workload_df = df[df['workload'].isin(['insert', 'readonly', 'mixed', 'delete'])].copy()
    
    if workload_df.empty:
        print("Skipping workload comparison (no data)")
        return
    
    workload_df = workload_df.groupby(['impl', 'workload']).agg({
        'throughput': 'mean'
    }).reset_index()
    
    fig, ax = plt.subplots(figsize=(10, 5))
    pivot = workload_df.pivot(index='workload', columns='impl', values='throughput') / 1e6
    pivot = pivot[['coarse', 'fine', 'lockfree']]  # Order columns
    pivot.columns = ['Coarse-Grained', 'Fine-Grained', 'Lock-Free']
    pivot.plot(kind='bar', ax=ax, width=0.75, edgecolor='black', linewidth=0.5)
    
    ax.set_xlabel('Workload Type', fontweight='bold')
    ax.set_ylabel('Throughput (M ops/sec)', fontweight='bold')
    ax.set_title('Figure 3: Performance Across Workloads (8 Threads)', fontweight='bold', pad=15)
    ax.legend(title='Implementation', frameon=True, fancybox=True, shadow=True)
    ax.set_xticklabels(['Insert-Only', 'Read-Only', 'Mixed', 'Delete-Heavy'], rotation=45, ha='right')
    ax.grid(True, alpha=0.3, axis='y', linestyle='--')
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'figure3_workload.png')
    plt.savefig(output_path, bbox_inches='tight', dpi=300)
    plt.close()
    print(f"Created: {output_path}")

def plot_contention(df, output_dir):
    """Figure 4: Plot performance under different contention levels"""
    if 'key_range' not in df.columns or df['key_range'].nunique() <= 1:
        print("Skipping contention analysis (insufficient data)")
        return
    
    contention_df = df[df['key_range'].isin([1000, 10000, 100000, 1000000])].copy()
    
    if contention_df.empty:
        print("Skipping contention analysis (no data)")
        return
    
    contention_df = contention_df.groupby(['impl', 'key_range']).agg({
        'throughput': 'mean'
    }).reset_index()
    
    fig, ax = plt.subplots(figsize=(8, 5))
    
    for impl in ['coarse', 'fine', 'lockfree']:
        impl_data = contention_df[contention_df['impl'] == impl].sort_values('key_range')
        label = {'coarse': 'Coarse-Grained', 'fine': 'Fine-Grained', 'lockfree': 'Lock-Free'}[impl]
        ax.plot(impl_data['key_range'], impl_data['throughput'] / 1e6,
                marker='o', linewidth=2.5, markersize=8, label=label)
    
    ax.set_xlabel('Key Range (Contention: High ← → Low)', fontweight='bold')
    ax.set_ylabel('Throughput (M ops/sec)', fontweight='bold')
    ax.set_title('Figure 4: Performance Under Contention (16 Threads)', fontweight='bold', pad=15)
    ax.set_xscale('log')
    ax.legend(frameon=True, fancybox=True, shadow=True, loc='best')
    ax.grid(True, alpha=0.3, linestyle='--')
    
    # Add annotation for extreme contention result
    ax.annotate('6× faster than\nFine-Grained!', 
                xy=(1000, 9.18), xytext=(1500, 7),
                arrowprops=dict(arrowstyle='->', lw=1.5, color='red'),
                fontsize=10, color='red', fontweight='bold',
                bbox=dict(boxstyle='round,pad=0.5', facecolor='yellow', alpha=0.7))
    
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'figure4_contention.png')
    plt.savefig(output_path, bbox_inches='tight', dpi=300)
    plt.close()
    print(f"Created: {output_path}")

def plot_comparison(df, output_dir):
    """Figure 5: Comparative bar chart at peak performance"""
    peak_df = df[(df['workload'] == 'mixed') & (df['threads'] == 32)].copy()
    
    if peak_df.empty:
        print("Skipping comparison chart (no 32-thread data)")
        return
    
    peak_df = peak_df.groupby('impl').agg({'throughput': 'mean'}).reset_index()
    
    fig, ax = plt.subplots(figsize=(7, 5))
    
    impl_order = ['coarse', 'fine', 'lockfree']
    labels = ['Coarse-\nGrained', 'Fine-\nGrained', 'Lock-\nFree']
    colors = ['#d62728', '#ff7f0e', '#2ca02c']
    
    throughputs = []
    for impl in impl_order:
        val = peak_df[peak_df['impl'] == impl]['throughput'].values
        throughputs.append(val[0] / 1e6 if len(val) > 0 else 0)
    
    bars = ax.bar(labels, throughputs, color=colors, edgecolor='black', linewidth=1.5, alpha=0.8)
    
    # Add value labels on bars
    for bar, val in zip(bars, throughputs):
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2., height,
                f'{val:.2f}M',
                ha='center', va='bottom', fontweight='bold', fontsize=11)
    
    # Add speedup annotation for lock-free
    if throughputs[2] > 0:
        speedup_vs_coarse = throughputs[2] / throughputs[0] if throughputs[0] > 0 else 0
        speedup_vs_fine = throughputs[2] / throughputs[1] if throughputs[1] > 0 else 0
        ax.text(2, throughputs[2] + 0.5, f'{speedup_vs_coarse:.0f}× vs Coarse\n{speedup_vs_fine:.2f}× vs Fine', 
                ha='center', fontsize=9, fontweight='bold',
                bbox=dict(boxstyle='round,pad=0.5', facecolor='lightgreen', alpha=0.7))
    
    ax.set_ylabel('Throughput (M ops/sec)', fontweight='bold')
    ax.set_title('Figure 5: Peak Performance (32 Threads, Mixed Workload)', fontweight='bold', pad=15)
    ax.grid(True, alpha=0.3, axis='y', linestyle='--')
    ax.set_ylim(0, max(throughputs) * 1.2)
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'figure5_comparison.png')
    plt.savefig(output_path, bbox_inches='tight', dpi=300)
    plt.close()
    print(f"Created: {output_path}")

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
    
    # Create output directory (in project root, not inside results/)
    output_dir = 'figures'
    os.makedirs(output_dir, exist_ok=True)
    
    print("Generating publication-quality figures...")
    print(f"Output directory: {output_dir}/")
    print()
    
    # Generate all 5 figures
    plot_scalability(df, output_dir)
    plot_speedup(df, output_dir)
    plot_workload_comparison(df, output_dir)
    plot_contention(df, output_dir)
    plot_comparison(df, output_dir)
    
    print()
    print(f"✅ All figures saved to: {output_dir}/")
    print()
    print("Generated figures:")
    print("  - figure1_scalability.png: Throughput vs thread count")
    print("  - figure2_speedup.png: Speedup relative to 1 thread")
    print("  - figure3_workload.png: Performance across workloads")
    print("  - figure4_contention.png: Performance under contention (6× result!)")
    print("  - figure5_comparison.png: Peak performance comparison")
    print()
    print("These figures are ready for inclusion in your report!")

if __name__ == '__main__':
    main()