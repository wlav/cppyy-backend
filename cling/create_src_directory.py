#!/usr/bin/env python
from __future__ import print_function

import os, sys, subprocess
import shutil, tarfile
try:
    import urllib2
except ModuleNotFoundError:
    import urllib.request as urllib2  # p3

if 'win32' in sys.platform:
    def rename(f1, f2):
        os.remove(f2)
        os.rename(f1, f2)
else:
    rename = os.rename

def is_manylinux():
    _is_manylinux = False
    try:
        for line in open('/etc/redhat-release').readlines():
            if 'CentOS release 6.10 (Final)' in line:
                _is_manylinux = True
                break
    except (OSError, IOError):
        pass
    return _is_manylinux


DEBUG_TESTBUILD = False
TARBALL_CACHE_DIR = 'releases'
ERR_RELEASE_NOT_FOUND = 2

ROOT_VERSION = '6.20.04'

#
## released source pull and copy of Cling
#
if not os.path.exists(TARBALL_CACHE_DIR):
    os.mkdir(TARBALL_CACHE_DIR)

fn = 'root_v%s.source.tar.gz' % ROOT_VERSION
addr = 'https://root.cern.ch/download/'+fn
if not os.path.exists(os.path.join(TARBALL_CACHE_DIR, fn)):
    try:
        print('retrieving', fn)
        if sys.hexversion < 0x3000000:
            output_fn = fn
        else:
            output_fn = bytes(fn, 'utf-8')
        resp = urllib2.urlopen(addr, output_fn)
        out = open(os.path.join(TARBALL_CACHE_DIR, fn), 'wb')
        out.write(resp.read())
        out.close()
    except urllib2.HTTPError:
        print('release %s not found' % ROOT_VERSION)
        sys.exit(ERR_RELEASE_NOT_FOUND)
else:
    print('reusing', fn, 'from local directory')


fn = os.path.join(TARBALL_CACHE_DIR, fn)
pkgdir = os.path.join('root-' + ROOT_VERSION)
if not os.path.exists(pkgdir):
    print('now extracting', ROOT_VERSION)
    tf = tarfile.TarFile.gzopen(fn)
    tf.extractall()
    tf.close()
else:
    print('reusing existing directory', pkgdir)


# remove old directoy, if any and enter release directory
try:
    shutil.rmtree(os.path.join('src', 'interpreter'))
except OSError:
    pass

#
## package creation
#
countdown = 0

print('adding src ... ')
if not os.path.exists('src'):
    os.mkdir('src')
fullp = os.path.join(pkgdir, 'interpreter')
dest = os.path.join('src', 'interpreter')
shutil.copytree(fullp, dest)

#
## apply patches (in order)
#
try:
    import patch
except ImportError:
    class patch(object):
        @staticmethod
        def fromfile(fdiff):
            return patch(fdiff)

        def __init__(self, fdiff):
            self.fdiff = fdiff

        def apply(self):
            res = os.system('patch -p1 < ' + self.fdiff)
            return res == 0


for fdiff in ('typedef_of_private', 'optlevel2_forced', 'explicit_template', 'alias_template',
              'incomplete_types', 'clang_printing', 'pch', 'win64rtti', 'win64', 'win64s2',
              'locales', 'gcc10'):
    fpatch = os.path.join('patches', fdiff+'.diff')
    print(' ==> applying patch:', fpatch)
    pset = patch.fromfile(fpatch)
    if not pset or not pset.apply():
        print("Failed to apply patch:", fdiff)
        sys.exit(2)

#
## manylinux1 specific patch, as there a different, older, compiler is used
#
if is_manylinux():
    print(' ==> applying patch:', 'manylinux1')
    patch.fromfile(os.path.join('patches', 'manylinux1.diff')).apply()

#
## finally, remove the ROOT source directory, as it can not be reused
#
print("removing", pkgdir)
shutil.rmtree(pkgdir)

# done!
