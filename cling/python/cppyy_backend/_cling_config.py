from __future__ import print_function
import os, sys, subprocess

MYHOME = os.path.dirname(__file__)

def main():
    if len(sys.argv) == 2 and \
           (sys.argv[1] == '--cflags' or sys.argv[1] == '--cppflags'):
        print('-I%s/include' % (MYHOME,))
        return 0
    else:
        print('Usage: cling-config [--cflags] [--cppflags]')
    return 1
