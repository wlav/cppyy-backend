from __future__ import print_function
import os, sys, subprocess

MYHOME = os.path.dirname(__file__)

def main():
    if len(sys.argv) == 2:
        if sys.argv[1] == '--cflags' or sys.argv[1] == '--cppflags':
            print('-I' + os.path.join(MYHOME, "include"))
            return 0
    print('Usage: cling-config [--cflags] [--cppflags]')
    return 1
