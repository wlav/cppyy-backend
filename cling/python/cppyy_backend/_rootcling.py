from __future__ import print_function
import os, sys, subprocess

MYHOME = os.path.dirname(__file__)

def main():
    if len(sys.argv) == 2 and \
           (sys.argv[1] == '--cflags' or sys.argv[1] == '--cppflags'):
        print('-I%s/include' % (MYHOME,))
        return 0
    os.environ['LD_LIBRARY_PATH'] = os.path.join(MYHOME, 'lib')
    rootcling = os.path.join(MYHOME, 'bin', 'rootcling')
    if not os.path.exists(rootcling):
        raise RuntimeError("rootcling not installed in standard location")
    return subprocess.call([rootcling] + sys.argv[1:])
