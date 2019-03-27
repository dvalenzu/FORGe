#!/usr/bin/python3
import sys
def print_usage(args):
    print (args[1], "SORTED.TXT VARS.VCF")

if __name__ == '__main__':
    n_args = len(sys.argv);
    if(n_args != 4):
        print_usage(sys.argv)
        sys.exit(1)

sorted_filename = sys.argv[1];
vcf_filename = sys.argv[2];
percent = sys.argv[3];

## OPEN SORTED
## SELECT PERCENT
## FILTER VCF
