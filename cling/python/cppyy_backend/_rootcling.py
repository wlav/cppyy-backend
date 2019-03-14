import os, sys, subprocess

MYHOME = os.path.dirname(__file__)

def main():
    os.environ['LD_LIBRARY_PATH'] = os.path.join(MYHOME, 'lib')
    rootcling = os.path.join(MYHOME, 'bin', 'rootcling')
    if 'win32' in sys.platform:
        rootcling += '.exe'
    if not os.path.exists(rootcling):
        raise RuntimeError("rootcling not installed in standard location")

    from ._get_cppflags import get_cppflags
    extra_flags = get_cppflags()
    if extra_flags is not None and 1 < len(sys.argv):
      # rootcling is picky about order ...
       args = [sys.argv[1], '-cxxflags', extra_flags] + sys.argv[2:]
    else:
       args = sys.argv[1:]

    return subprocess.call([rootcling] + args)


if __name__ == "__main__":
    sys.exit(main())
