from __future__ import print_function
""" cppyy_backend loader
"""

import os, sys, ctypes, subprocess

if 'win' in sys.platform:
   soext = '.dll'
else:
   soext = '.so'

def load_cpp_backend():
    if 'linux' in sys.platform:
     # the precompiled header of standard and system headers is not part of the
     # distribution as there are too many varieties; create it now if needed
        _ensure_precompiled_header()

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

def _set_cling_compile_options():
 # extra optimization flags for Cling
    if not 'EXTRA_CLING_ARGS' in os.environ:
        has_avx = False
        try:
            for line in open('/proc/cpuinfo', 'r'):
                if 'avx' in line:
                    has_avx = True
                    break
        except Exception:
            try:
                cli_arg = subprocess.check_output(['sysctl', 'machdep.cpu.features'])
                has_avx = 'avx' in cli_arg.decode("utf-8").strip().lower()
            except Exception:
                pass
        extra_args = '-O2'
        if has_avx: extra_args += ' -mavx'
        os.putenv('EXTRA_CLING_ARGS', extra_args)

def _ensure_precompiled_header():
     # the precompiled header of standard and system headers is not part of the
     # distribution as there are too many varieties; create it now if needed
        olddir = os.getcwd()
        try:
            pkgpath = os.path.dirname(__file__)
            os.chdir(pkgpath)
            pchname = 'allDict.cxx.pch'
            if not os.path.exists(os.path.join('etc', pchname)):
                if os.access(os.path.join(pkgpath, 'etc'), os.R_OK|os.W_OK):
                    print('Building pre-compiled headers; this make take a minute ...')
                    _set_cling_compile_options()
                    makepch = os.path.join('etc', 'dictpch', 'makepch.py')
                    incpath = os.path.join(pkgpath, 'include')
                    if subprocess.call(['python', makepch, os.path.join('etc', pchname), '-I'+incpath]) != 0:
                        import warnings
                        warnings.warn('Failed to build precompiled header; this may impact performance.')
                else:
                    import warnings
                    warnings.warn('No precompiled header available; this may impact performance.')
        finally:
            os.chdir(olddir)
