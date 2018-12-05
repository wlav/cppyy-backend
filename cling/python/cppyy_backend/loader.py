""" cppyy_backend loader
"""

import os, sys, ctypes

if 'win' in sys.platform:
   soext = '.dll'
else:
   soext = '.so'

def load_cpp_backend():
    try:
      # normal load, allowing for user overrides of LD_LIBRARY_PATH
        c = ctypes.CDLL('libcppyy_backend'+soext, ctypes.RTLD_GLOBAL)
    except OSError:
      # failed ... load dependencies explicitly
        pkgpath = os.path.dirname(__file__)
        for dep in ['liblzma', 'libCore', 'libThread', 'libRIO', 'libCling']:
            for loc in ['lib', 'bin']:
                fpath = os.path.join(pkgpath, loc, dep+soext)
                if os.path.exists(fpath):
                    dep = fpath
                    ctypes.CDLL(dep, ctypes.RTLD_GLOBAL)
                    break
        c = ctypes.CDLL(os.path.join(pkgpath, 'lib', 'libcppyy_backend'+soext), ctypes.RTLD_GLOBAL)

    return c
