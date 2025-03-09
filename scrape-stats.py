#!/usr/bin/env python3

import argparse
import os
import re
import matplotlib.pyplot as plt

infiles = [
    {
        'path': './out_1c_stride1/stats.txt',
        'prefix': '1c-stride1'
    },
    {
        'path': './out_2c_stride1/stats.txt',
        'prefix': '2c-stride1'
    },
    {
        'path': './out_4c_stride1/stats.txt',
        'prefix': '4c-stride1'
    },
    {
        'path': './out_8c_stride1/stats.txt',
        'prefix': '8c-stride1'
    },
    {
        'path': './out_1c_stride16/stats.txt',
        'prefix': '1c-stride16'
    },
    {
        'path': './out_2c_stride16/stats.txt',
        'prefix': '2c-stride16'
    },
    {
        'path': './out_4c_stride16/stats.txt',
        'prefix': '4c-stride16'
    },
    {
        'path': './out_8c_stride16/stats.txt',
        'prefix': '8c-stride16'
    }
]

plot_list = [
     {
        'title': 'Cycles per Instruction',
        'filename': 'cpi.png',
        'subplots' : [
            {
                'title': 'CPU0',
                're': r'system.cpu[0]*.cpi\s+(\S+)',
                'y-label': 'CPI'
            }
        ]
    },
     {
        'title': 'CPU Cycles',
        'filename': 'cycles.png',
        'subplots' : [
            {
                'title': 'CPU0',
                're': r'system.cpu[0]*.numCycles\s+(\S+)',
                'y-label': 'cycles'
            }
        ]
    },
    {
        'title': 'L1 D-Cache Loads',
        'filename': 'l1-loads.png',
        'subplots' : [
            {
                'title': 'CPU0',
                're': r'system.cpu[0]*.l1d.inTransLatHist.Load::total\s+(\S+)',
                'y-label': 'count'
            }
        ]
    },
    {
        'title': 'CPU0 L1 D-Cache Snoop Traffic',
        'filename': 'l1-traffic.png',
        'subplots' : [
            {
                'title': 'SnpCleanInvalid',
                'y-scale': 'log',
                're': r'system\.cpu[0]*\.l1d\.inTransLatHist\.SnpCleanInvalid::total\s+(\S+)',
                'y-label': 'count'
            },
            {
                'title': 'SnpShared',
                'y-scale': 'log',
                're': r'system\.cpu[0]*\.l1d\.inTransLatHist\.SnpShared::total\s+(\S+)',
                'y-label': 'count'
            }
        ]
    },
    {
        'title': 'CPU0 L2 Snoop Traffic',
        'filename': 'l2-traffic.png',
        'subplots' : [
            {
                'title': 'SnpCleanInvalid',
                'y-scale': 'log',
                're': r'system\.cpu[0]*\.l2\.inTransLatHist\.SnpCleanInvalid::total\s+(\S+)',
                'y-label': 'count'
            },
            {
                'title': 'SnpSharedFwd',
                'y-scale': 'log',
                're': r'system\.cpu[0]*\.l2\.inTransLatHist\.SnpSharedFwd::total\s+(\S+)',
                'y-label': 'count'
            }
        ]
    },
    {
        'title': 'ReadShared at HNFs',
        'filename': 'hnf-readshared.png',
        'subplots' : [
            {
                'title': 'HNF0',
                're': r'system\.ruby\.hnf0\.cntrl\.inTransLatHist\.ReadShared::samples\s+(\S+)',
                'y-label': 'count'
            },
            {
                'title': 'HNF1',
                're': r'system\.ruby\.hnf1\.cntrl\.inTransLatHist\.ReadShared::samples\s+(\S+)',
                'y-label': 'count'
            }
        ]
    },
    {
        'title': 'ReadUnique at HNFs',
        'filename': 'hnf-readunique.png',
        'subplots' : [
            {
                'title': 'HNF0',
                're': r'system\.ruby\.hnf0\.cntrl\.inTransLatHist\.ReadUnique_PoC::samples\s+(\S+)',
                'y-label': 'count'
            },
            {
                'title': 'HNF1',
                're': r'system\.ruby\.hnf1\.cntrl\.inTransLatHist\.ReadUnique_PoC::samples\s+(\S+)',
                'y-label': 'count'
            }
        ]
    },
    {
        'title': 'CleanUnique at HNFs',
        'filename': 'hnf-cleanunique.png',
        'subplots' : [
            {
                'title': 'HNF0',
                're': r'system\.ruby\.hnf0\.cntrl\.inTransLatHist\.CleanUnique::samples\s+(\S+)',
                'y-label': 'count'
            },
            {
                'title': 'HNF1',
                're': r'system\.ruby\.hnf1\.cntrl\.inTransLatHist\.CleanUnique::samples\s+(\S+)',
                'y-label': 'count'
            }
        ]
    }
]

def gridpos(grid_rows, grid_cols, col_idx):
    return (100 * grid_rows) + (10 * grid_cols) + col_idx


def read_all_stats(infiles):
    stats_list=[]
    for entry in infiles:
        with open(entry['path'], 'r') as f:
            sd = {
                'prefix': entry['prefix'],
                'path': entry['path'],
                'lines': f.readlines()
            }
            stats_list.append(sd)
    return stats_list


def extract_data(stats_list, reg_exp, names, values, title):
    r = re.compile(reg_exp)
    for se in stats_list:
        m = None
        prefix=se['prefix']
        #print(f'looking for {reg_exp} in {prefix}')
        for line in se['lines']:
            m = r.match(line)
            if m:
                break
        if not m:
            print(f'warning no match for {title} in {se["path"]} for {reg_exp}')
            stat_val=0.0
        else:
            groups = m.groups()
            if len(groups) == 0:
                print(f'warning no match groups for {title} in {se["path"]} for {reg_exp}')
                stat_val=0.0
            else:
                stat_val=float(m.group(1))
            #print(f'found {prefix} line {line} stat {stat_val}')
            print(f'found {title} {prefix} stat {stat_val}')
        names.append(prefix)
        values.append(stat_val)


def extract_plot_data(stats_list, plot_list):
    for plot in plot_list:
        for sp in plot['subplots']:
            sp['names']=[]
            sp['values']=[]
            title = f'{plot["title"]}-{sp["title"]}'
            extract_data(stats_list, sp['re'], sp['names'], sp['values'], title)


def make_plot(plot, outdir):
    grid_rows=1
    grid_cols=len(plot['subplots'])

    plt.figure(figsize=(9, 3)) # todo: do we have to hardcode figure size?

    col_idx=1
    #for ds in data:
    for sp in plot['subplots']:
        sp_pos = gridpos(grid_rows, grid_cols, col_idx)
        #print(f'sp_pos is {sp_pos}')
        ax = plt.subplot(sp_pos, title=sp['title'])
        plt.bar(sp['names'], sp['values'])
        if 'y-scale' in sp:
            ax.set_yscale(sp['y-scale'])
        if 'x-scale' in sp:
            ax.set_xscale(sp['x-scale'])
        if 'y-label' in sp:
            plt.ylabel(sp['y-label'])
        plt.setp(ax.get_xticklabels(), rotation=30, horizontalalignment='right')
        col_idx=col_idx+1
    
    plt.suptitle(plot['title'])
    plt.tight_layout()
    out_file = os.path.join(outdir, plot['filename'])
    plt.savefig(out_file)
    print(f"wrote {out_file}")


def make_all_plots(plot_list, outdir):
    for plot in plot_list:
        make_plot(plot, outdir)



def scrape_stats(infiles, plot_list, outdir):
    # read stats files into list of lines
    stats_list = read_all_stats(infiles)

    # extract the values we care about, insert the names+values back into the plot_list
    extract_plot_data(stats_list, plot_list)

    # and plot them
    make_all_plots(plot_list, outdir)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--outdir",
        "-o",
        type=str,
        default=".",
        help="output directory to write",
    )

    args = parser.parse_args()
    os.makedirs(args.outdir, exist_ok=True)
    scrape_stats(infiles, plot_list, args.outdir)
