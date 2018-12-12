"""
Software artifacts for SCADET: A Side-Channel Attack Detection Tool for Tracking Prime+Probe.

This is the analysis pyspark code.

MIT License



Copyright (c) 2018 Majid Sabbagh



Permission is hereby granted, free of charge, to any person obtaining a copy

of this software and associated documentation files (the "Software"), to deal

in the Software without restriction, including without limitation the rights

to use, copy, modify, merge, publish, distribute, sublicense, and/or sell

copies of the Software, and to permit persons to whom the Software is

furnished to do so, subject to the following conditions:



The above copyright notice and this permission notice shall be included in all

copies or substantial portions of the Software.



THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR

IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,

FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE

AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER

LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,

OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE

SOFTWARE.
"""

# Import required packages
import sys
import argparse
import operator
import numpy as np

from sklearn.cluster import MeanShift, estimate_bandwidth
from collections import Counter
from scipy.stats import itemfreq
from struct import unpack_from

from pyspark import SparkContext, SparkConf


# Cache configuration parameters
KB = 1024
MB = 1024*KB

THRESHOLD = 90

PAGE_SIZE = 4*KB
# Ucomment line below for 2 MB huge page analysis 
#PAGE_SIZE = 2*MB 

NUM_CORE = 4# Number of "physical" cores

## Write and read flags
WR = 0
RD = 1

## Minimum number of Prime+Probe couples to detect
NUM_PP = 1

## L1-info -- modify it based on your system
L1_CACHE_SIZE = 32*KB
L1_CACHE_LINE_SIZE = 64
L1_CACHE_LINE_BITS = int(np.log2(L1_CACHE_LINE_SIZE))
L1_CACHE_ASSOC = 8
L1_CACHE_SET_SIZE = L1_CACHE_ASSOC*L1_CACHE_LINE_SIZE
L1_CACHE_SET_BITS = int(np.log2(L1_CACHE_SET_SIZE))
L1_CACHE_SET = L1_CACHE_SIZE/L1_CACHE_ASSOC/L1_CACHE_LINE_SIZE
L1_SET_MASK = L1_CACHE_SIZE / (L1_CACHE_ASSOC * L1_CACHE_LINE_SIZE) - 1
PHYS_ADDR_MASK_L1 = (1 << int(np.log2(PAGE_SIZE) - np.log2(L1_CACHE_LINE_SIZE))) - 1
NUMBER_OF_L1_SETS = L1_CACHE_SET

## L3-info -- modify it based on your system
L3_CACHE_SIZE = 8*MB
L3_CACHE_LINE_SIZE = 64
L3_CACHE_LINE_BITS = int(np.log2(L3_CACHE_LINE_SIZE))
L3_CACHE_ASSOC = 16
L3_CACHE_SET_SIZE = L3_CACHE_ASSOC*L3_CACHE_LINE_SIZE
L3_CACHE_SET_BITS = int(np.log2(L3_CACHE_SET_SIZE))
L3_CACHE_SET = L3_CACHE_SIZE/L3_CACHE_ASSOC/L3_CACHE_LINE_SIZE
L3_SET_MASK = L3_CACHE_SIZE / (L3_CACHE_ASSOC * NUM_CORE * L3_CACHE_LINE_SIZE) - 1
PHYS_ADDR_MASK_L3 = (1 <<  int(np.log2(PAGE_SIZE) - np.log2(L3_CACHE_LINE_SIZE))) - 1
NUMBER_OF_L3_SETS = L3_CACHE_SET/NUM_CORE

L1_INTRA_GROUP_THRESHOLD = 2 # Default: 2
L3_INTRA_GROUP_THRESHOLD = 24 # Default: 24

L1_SET_DIFF = 3600 # Default: 3600
LLC_SET_DIFF = 10000 # Default: 10000

# Initializing parameters
scope = 'L1I'
CACHE_ASSOC = L1_CACHE_ASSOC
INTRA_GROUP_THRESHOLD = L1_INTRA_GROUP_THRESHOLD
CACHE_LINE_BITS = L1_CACHE_LINE_BITS
SET_MASK = L1_SET_MASK
CACHE_SET_BITS = L1_CACHE_SET_BITS

# Helper functions
# Calculating cache set index from physical address
def paddrToSetIdx(paddr_dict):
    paddr = paddr_dict['addrs']
    _, _, line_bits, set_mask, _, _ = paddr_dict['params']
    return np.bitwise_and(np.right_shift(paddr, line_bits), set_mask)

# Calculating cache line tag from physical address
def paddrToTag(paddr_dict):
    paddr = paddr_dict['addrs']
    _, _, line_bits, _, set_bits, _ = paddr_dict['params']
    return np.right_shift(paddr, set_bits + line_bits)

# Clustering cache line tags 
def cluster_gen(uc_data_dict):
    tags = set() # Cache line tags for each cluster
    set_tags = set() # Set of tags (set of list) for different clusters
    times = uc_data_dict['times']
    addrs = uc_data_dict['addrs']
    cache_assoc, intra_group_threshold, _, _, _, inter_group_threshold = uc_data_dict['params']
    cluster_mean = times[0]
    init = 0
    cnt = 1 # number of members in the cluster
    for i in range(1,len(times)):
        # We already filtered out the sets with < no of ways unique line addresses
        diff = times[i] - times[i-1]
        if(diff<intra_group_threshold and diff>0):
            tags.update([addrs[i-1], addrs[i]])
            cluster_mean = cluster_mean + times[i]
            cnt = cnt + 1
        if(diff>=intra_group_threshold or i == (len(times)-1)):
            cluster_mean = cluster_mean/cnt
            no_of_unique_lines = len(tags)
            if(no_of_unique_lines>=cache_assoc):
                if inter_group_threshold is None:
                    set_tags.add((no_of_unique_lines, cluster_mean))
                elif(init == 0):
                    temp_tuple = (no_of_unique_lines, cluster_mean)
                    prev_mean = temp_tuple[1]
                    init = 1
                else:
                    if( abs(cluster_mean - prev_mean) <= inter_group_threshold ):
                        prev_mean = cluster_mean
                        set_tags.update([temp_tuple, (no_of_unique_lines, cluster_mean)])
                    temp_tuple = (no_of_unique_lines, cluster_mean)
            cluster_mean = times[i]
            tags = set()
            cnt = 1
    return list(set_tags)


# top level 
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description = 'Parallel address analysis tool',formatter_class=argparse.ArgumentDefaultsHelpFormatter)    
    parser.add_argument('--mode', help='Mode of operation', choices=['BOTH', 'READ', 'WRITE']) 
    parser.add_argument('scope', help='Target scope (cache level and instruction/data)', choices=['L1I', 'L1D', 'LLC'])
    parser.add_argument('input', help='Input file or list of files.')
    parser.add_argument('--verbose', help='Generate a verbose report.')
    
    args = parser.parse_args()
    conf = (SparkConf().set("spark.driver.maxResultSize", "20g").setAppName("SCADET-ext"))
    sc = SparkContext(conf=conf)

    mode = args.mode;
    scope = args.scope;
    
    result_file = open(args.input + "_result.txt", 'a')    

    result_file.write(str(mode) + ', ' + scope + '\n\n')

    if (scope in ['L1I', 'L1D']):
        CACHE_ASSOC = L1_CACHE_ASSOC
        INTRA_GROUP_THRESHOLD = L1_INTRA_GROUP_THRESHOLD
        CACHE_LINE_BITS = L1_CACHE_LINE_BITS
        SET_MASK = L1_SET_MASK
        CACHE_SET_BITS = L1_CACHE_SET_BITS
        if (scope in ['L1D']):
            INTER_GROUP_THRESHOLD = L1_SET_DIFF
        else:
            INTER_GROUP_THRESHOLD = L1_SET_DIFF
    if (scope == 'LLC'):
        CACHE_ASSOC = L3_CACHE_ASSOC
        INTRA_GROUP_THRESHOLD = L3_INTRA_GROUP_THRESHOLD
        CACHE_LINE_BITS = L3_CACHE_LINE_BITS
        SET_MASK = L3_SET_MASK
        CACHE_SET_BITS = L3_CACHE_SET_BITS
        INTER_GROUP_THRESHOLD = LLC_SET_DIFF

    CACHE_PARAMS = (CACHE_ASSOC, INTRA_GROUP_THRESHOLD, CACHE_LINE_BITS, SET_MASK, CACHE_SET_BITS, INTER_GROUP_THRESHOLD)
    
    FILTER_THRESHOLD = CACHE_ASSOC;

    result_file.write('Cache parameters (assoc, intra_group_threshold, line_bits, set_mask, set_bits, inter_group_threshold): ' + str(CACHE_PARAMS) + '\n\n')

    if (scope == 'L1I'):
        # 1st Q: Program counter
        unpack_format = '1Q' #one 64-bit values
        NUM_OF_ITEMS = 1 #Number of items per record (line) of the binary file
    else:
        # 1st Q: R/W | 2nd Q: Memory addresses
        unpack_format = '2Q' #two 64-bit values
        NUM_OF_ITEMS = 2 #Number of items per record (line) of the binary file

    NUM_BYTES_PER_ITEM = 8 #Number of bytes per item
    recordLength = NUM_OF_ITEMS * NUM_BYTES_PER_ITEM #Total number of bytes in each record (line)
    binary_rdd = sc.binaryRecords(args.input, recordLength)
    
    set_grouped_rdd = binary_rdd.map(lambda record: unpack_from(unpack_format, record)).zipWithIndex()
    set_grouped_rdd.cache

############################ *************************** L1D or LLC read analysis *************************** #########################################
########################################################################################################################################################
########################################################################################################################################################
    if (scope in ['L1D', 'LLC']):   
        if (mode in ['READ', 'BOTH']):    
            # mem access addresses are at the second column
            set_grouped_rdd_read = set_grouped_rdd.filter(lambda (items, index): items[0] == RD) \
            .map(lambda (items, index): (paddrToSetIdx({'addrs': items[1], 'params': CACHE_PARAMS}), \
            (index, paddrToTag({'addrs': items[1], 'params': CACHE_PARAMS})))).groupByKey().map(lambda x : (x[0], list(x[1])))
            # Create [(number of unique address lines in group0, average access time in group0), ...], filter by temporal locality in the line access times and number of the unique addresses
            cnts_intrasetTimes_rdd = set_grouped_rdd_read.filter(lambda (x, y): len(set(elem[1] for elem in y))>=FILTER_THRESHOLD) \
            .mapValues(lambda values: cluster_gen({'times': list(elem[0] for elem in values), 'addrs': list(elem[1] for elem in values), \
            'params': CACHE_PARAMS})).filter(lambda (x, y): len(y)>NUM_PP) #Threshold is architecture dependent, 5820000

            cnts_times = cnts_intrasetTimes_rdd.collect()
            result_table = 'Data cache read mode detected set idx | (#lines, cluster_center)' \
                         + '\n=========================================================================='
            for line in cnts_times:
                result_table = result_table + '\n' + ' | '.join(map(str, [line[0], line[1][0]])) + '\n------------------------------------------------'
            result_file.write(result_table + '\n\n')
            print('\n\n*********Result is here*********\n\n')
            if cnts_times:
                target_sets = list(x[0] for x in cnts_times)
                result_file.write('Sets: ' + str(target_sets) + '\n\n')
                result_file.write('#Sets: ' + str(len(target_sets)) + '\n\n')
                result_file.write('prime+probe pattern is detected!\n\n')
            else:
                result_file.write('No prime+probe pattern is detected!\n\n')
     
############################ *************************** L1D or LLC write analysis *************************** #########################################
########################################################################################################################################################
########################################################################################################################################################
        if (mode in ['WRITE', 'BOTH']):        
            # mem access addresses are at the third column
            set_grouped_rdd_write = set_grouped_rdd.filter(lambda (items, index): items[0] == WR) \
            .map(lambda (items, index): (paddrToSetIdx({'addrs': items[1], 'params': CACHE_PARAMS}), \
            (index, paddrToTag({'addrs': items[1], 'params': CACHE_PARAMS})))).groupByKey().map(lambda x : (x[0], list(x[1]))) 
            # Create [(number of unique address lines in group0, average access time in group0), ...], filter by temporal locality in the line access times and number of the unique addresses
            cnts_intrasetTimes_rdd = set_grouped_rdd_write.filter(lambda (x, y): len(set(elem[1] for elem in y))>=FILTER_THRESHOLD) \
            .mapValues(lambda values: cluster_gen({'times': list(elem[0] for elem in values), 'addrs': list(elem[1] for elem in values), \
            'params': CACHE_PARAMS})).filter(lambda (x, y): len(y)>NUM_PP)
                
            cnts_times = cnts_intrasetTimes_rdd.collect()
            result_table = 'Data cache write mode detected set idx | (#lines, cluster_center)' \
                         + '\n=========================================================================='
            for line in cnts_times:
                result_table = result_table + '\n' + ' | '.join(map(str, [line[0], line[1][0]])) + '\n------------------------------------------------'
            result_file.write(result_table + '\n\n')
            print('\n\n*********Result is here*********\n\n')
            if cnts_times:
                target_sets = list(x[0] for x in cnts_times)
                result_file.write('Sets: ' + str(target_sets) + '\n\n')
                result_file.write('#Sets: ' + str(len(target_sets)) + '\n\n')
                result_file.write('prime+probe pattern is detected!\n\n')
            else:
                result_file.write('No prime+probe pattern is detected!\n\n')
############################ ********************************* L1I analysis ********************************** #########################################
########################################################################################################################################################
########################################################################################################################################################
    if (scope == 'L1I'):
        set_grouped_rdd_I = set_grouped_rdd.map(lambda (items, index): (paddrToSetIdx({'addrs': items[0], 'params': CACHE_PARAMS}), \
        (index, paddrToTag({'addrs': items[0], 'params': CACHE_PARAMS}), items[0]))).groupByKey().map(lambda x : (x[0], list(x[1])))
        # Create [(number of unique address lines in group0, average access time in group0), ...], filter by temporal locality in the line access times and number of the unique addresses
        cnts_intrasetTimes_rdd = set_grouped_rdd_I.filter(lambda (x, y): len(set(elem[1] for elem in y))>=FILTER_THRESHOLD) \
        .mapValues(lambda values: (cluster_gen({'times': list(elem[0] for elem in values), 'addrs': list(elem[1] for elem in values), \
        'params': CACHE_PARAMS}))).filter(lambda (x, y): len(y[0])>NUM_PP) #Threshold is architecture dependent, 5820000
           
        cnts_times = cnts_intrasetTimes_rdd.collect()
        result_table = 'L1I detected set idx | (#lines, cluster_center)' \
                     + '\n=========================================================================='
        #hot_times = set()
        for line in cnts_times:
            result_table = result_table + '\n' + ' | '.join(map(str, [line[0], line[1][0]])) + '\n------------------------------------------------'
        result_file.write(result_table + '\n\n')
        print('*********Result is here*********')
        if cnts_times:
            target_sets = list(x[0] for x in cnts_times)
            result_file.write('Sets: ' + str(target_sets) + '\n\n')
            result_file.write('#Sets: ' + str(len(target_sets)) + '\n\n')
            result_file.write('prime+probe pattern is detected!\n\n')
        else:
            result_file.write('No prime+probe pattern is detected!\n\n')


    result_file.close()
