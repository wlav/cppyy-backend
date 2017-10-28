#!/usr/bin/env python

import os, glob, subprocess
from setuptools import setup, find_packages, Extension
from distutils import log
from distutils.command.build_ext import build_ext as _build_ext
from distutils.command.clean import clean as _clean
from distutils.dir_util import remove_tree
from wheel.bdist_wheel import bdist_wheel as _bdist_wheel
from codecs import open


here = os.path.abspath(os.path.dirname(__file__))
with open(os.path.join(here, 'README.rst'), encoding='utf-8') as f:
    long_description = f.read()

try:
    root_install = os.environ["ROOTSYS"]
    requirements = []
    add_pkg = ['cppyy_backend']
except KeyError:
    root_install = None
    requirements = ['cppyy-cling']
    add_pkg = []

def get_include_path():
    if root_install:
        return os.path.join(root_install, 'include')
    cli_arg = subprocess.check_output(['cling-config', '--cppflags'])
    return cli_arg[2:-1].decode("utf-8")

class my_build_cpplib(_build_ext):
    def build_extension(self, ext):
        include_dirs = ext.include_dirs + [get_include_path()]
        log.info('checking for %s', self.build_temp)
        if not os.path.exists(self.build_temp):
            log.info('creating %s', self.build_temp)
            os.makedirs(self.build_temp)
        objects = self.compiler.compile(
            ext.sources,
            output_dir=self.build_temp,
            include_dirs=include_dirs,
            debug=self.debug,
            extra_postargs=['-std=c++11', '-O2'])

        ext_path = self.get_ext_fullpath(ext.name)
        output_dir = os.path.dirname(ext_path)
        full_libname = 'libcppyy_backend.so' # forced, b/c hard-wired in pypy-c/cppyy

        log.info("now building %s", full_libname)
        self.compiler.link_shared_object(
            objects, full_libname,
            build_temp=self.build_temp,
            output_dir=output_dir,
            debug=self.debug,
            target_lang='c++')

class my_clean(_clean):
    def run(self):
        # Custom clean. Clean everything except that which the base clean
        # (see below) or create_src_directory.py is responsible for.
        topdir = os.getcwd()
        if self.all:
            # remove build directories
            for directory in (os.path.join(topdir, "dist"),
                              os.path.join(topdir, "python", "cppyy_backend.egg-info")):
                if os.path.exists(directory):
                    remove_tree(directory, dry_run=self.dry_run)
                else:
                    log.warn("'%s' does not exist -- can't clean it",
                             directory)
        # Base clean.
        _clean.run(self)

class my_bdist_wheel(_bdist_wheel):
    def finalize_options(self):
     # this is a universal, but platform-specific package; a combination
     # that wheel does not recognize, thus simply fool it
        from distutils.util import get_platform
        self.plat_name = get_platform()
        self.universal = True
        _bdist_wheel.finalize_options(self)
        self.root_is_pure = True


setup(
    name='cppyy-backend',
    description='C/C++ wrapper for Cling',
    long_description=long_description,
    url='http://pypy.org',

    # Author details
    author='PyPy Developers',
    author_email='pypy-dev@python.org',

    version='0.3.5',

    license='LBNL BSD',

    classifiers=[
        'Development Status :: 4 - Beta',

        'Intended Audience :: Developers',
        'Intended Audience :: Science/Research',

        'Topic :: Software Development',
        'Topic :: Software Development :: Interpreters',

        'License :: OSI Approved :: BSD License',

        'Operating System :: POSIX',
        'Operating System :: POSIX :: Linux',
        'Operating System :: MacOS :: MacOS X',

        'Programming Language :: C',
        'Programming Language :: C++',

        'Natural Language :: English'
    ],

    keywords='C++ bindings',

    setup_requires=requirements,

    package_dir={'': 'python'},
    packages=find_packages('python', include=add_pkg),

    ext_modules=[Extension('cppyy_backend/lib/libcppyy_backend',
        sources=glob.glob('src/clingwrapper.cxx'))],
    zip_safe=False,

    cmdclass = {
        'build_ext': my_build_cpplib,
        'clean': my_clean,
        'bdist_wheel': my_bdist_wheel
    }
)
