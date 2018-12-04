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
        libpath = os.path.join(os.path.dirname(__file__), 'lib')
        for dep in ['libCore', 'libThread', 'libRIO', 'libCling']:
            fpath = os.path.join(libpath, dep+soext)
            if os.path.exists(fpath):
                dep = fpath
            ctypes.CDLL(dep, ctypes.RTLD_GLOBAL)
        c = ctypes.CDLL(os.path.join(libpath, 'libcppyy_backend'+soext), ctypes.RTLD_GLOBAL)

    return c
