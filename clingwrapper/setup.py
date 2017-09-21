#!/usr/bin/env python

import os, glob, subprocess
from setuptools import setup
from distutils import log
from distutils.command.build_clib import build_clib as _build_clib
from setuptools.command.install import install as _install
from wheel.bdist_wheel import bdist_wheel as _bdist_wheel
from codecs import open


here = os.path.abspath(os.path.dirname(__file__))
with open(os.path.join(here, 'README.rst'), encoding='utf-8') as f:
    long_description = f.read()


def get_include_path():
    cli_arg = subprocess.check_output(['cling-config', '--cppflags'])
    return cli_arg[2:-1]

libcppyy_backend = ('cppyy_backend',
                    {'sources' : ['src/clingwrapper.cxx'],
                     'include_dirs' : [get_include_path()]}
                    )

class my_build_cpplib(_build_clib):
    def build_libraries(self, libraries):
        for (lib_name, build_info) in libraries:
            sources = list(build_info.get('sources'))
            include_dirs = build_info.get('include_dirs')
            extra_args = ['-std=c++11', '-O2']

            objects = self.compiler.compile(
                sources,
                output_dir=self.build_temp,
                include_dirs=include_dirs,
                debug=self.debug,
                extra_postargs=extra_args)

            full_libname = self.compiler.shared_lib_format %\
                           (lib_name, self.compiler.shared_lib_extension)

            log.info("Now building %s", full_libname)
            self.compiler.link_shared_object(
                objects, full_libname,
                build_temp=self.build_temp,
                output_dir=self.build_clib,
                debug=self.debug,
                target_lang='c++')

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
    name='clingwrapper',
    description='C/C++ wrapper for Cling',
    long_description=long_description,
    url='http://pypy.org',

    # Author details
    author='PyPy Developers',
    author_email='pypy-dev@python.org',

    use_scm_version=True,
    setup_requires=['setuptools_scm', 'cppyy_backend'],

    license='LBNL BSD',

    classifiers=[
        'Development Status :: 4 - Beta',

        'Intended Audience :: Developers',
        'Intended Audience :: Science/Research',

        'Topic :: Software Development',
        'Topic :: Software Development :: Interpreters',

        'License :: OSI Approved :: LBNL BSD License',

        'Operating System :: POSIX',
        'Operating System :: POSIX :: Linux',
        'Operating System :: MacOS :: MacOS X',

        'Programming Language :: C',
        'Programming Language :: C++',

        'Natural Language :: English'
    ],

    keywords='C++ bindings',

    install_requires=['cppyy-backend'],
    libraries = [libcppyy_backend],

    cmdclass = {
        'build_clib': my_build_cpplib,
        'bdist_wheel': my_bdist_wheel
    }
)
