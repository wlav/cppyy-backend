from __future__ import print_function
import os, sys, subprocess

MYHOME = os.path.dirname(__file__)

def main():
    if len(sys.argv) == 2:
        options = sys.argv[1]

        if options == '--cmake':
            print(os.path.join(MYHOME, "cmake"))
            return 0

        if options == '--cppflags':
            options = '--cflags'

        if options != '--help':
            rcfg = os.path.join(MYHOME, 'bin', 'root-config')
            try:
                cli_arg = subprocess.check_output(
                    [os.path.join(MYHOME, 'bin', 'root-config'), options],
                    stderr=subprocess.STDOUT)
                print(cli_arg.decode("utf-8").strip())
                return 0
            except OSError:
                if not os.path.exists(rcfg) or not 'win' in sys.platform:
                    raise

                # happens on Windows b/c root-config is a bash script; the
                # following covers the most important options until that
                # gets fixed upstream

                def get_include_dir():
                    return os.path.join(MYHOME, 'include')

                def get_stdflags():
                    for line in open(rcfg):
                        if 'cxxversion' in line:
                            if 'cxx11' in line:
                                return '/std:c++11'
                            elif 'cxx14' in line:
                                return '/std:c++14'
                            elif 'cxx17' in line:
                                return '/std:c++17'
                            else:
                                return '/std:c++latest'
                    raise

                if options == '--incdir':
                    print(get_include_dir())
                    return 0

                elif options == '--auxcflags':
                # most important is get the C++ version flag right
                    print(get_stdflags())
                    return 0

                elif options == '--cflags':
                # most important are C++ flag and include directory
                    print(get_stdflags(), '-I'+get_include_dir())
                    return 0

            except subprocess.CalledProcessError:
                pass

    print('Usage: cling-config [--cflags] [--cppflags] [--cmake]')
    return 1

if __name__ == '__main__':
    sys.exit(main())
