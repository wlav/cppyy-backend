import os, sys, subprocess
from setuptools import setup, find_packages
from distutils import log
from distutils.command.build import build as _build
from setuptools.command.install import install as _install
from distutils.sysconfig import get_python_lib
from distutils.errors import DistutilsSetupError
from codecs import open

here = os.path.abspath(os.path.dirname(__file__))
with open(os.path.join(here, 'README.rst'), encoding='utf-8') as f:
    long_description = f.read()

builddir = None
def get_builddir():
    global builddir
    if builddir is None:
        topdir = os.getcwd()
        builddir = os.path.join(topdir, 'builddir')
    return builddir

srcdir = None
def get_srcdir():
    global srcdir
    if srcdir is None:
        topdir = os.getcwd()
        srcdir = os.path.join(topdir, 'src', 'backend')
    return srcdir

class my_cmake_build(_build):
    def __init__(self, dist, *args, **kwargs):
        _build.__init__(self, dist, *args, **kwargs)
        # TODO: can't seem to find a better way of getting hold of
        # the install_lib parameter during the build phase ...
        prefix = ''
        try:
            prefix = dist.get_command_obj('install').install_lib
        except AttributeError:
            pass
        if not prefix:
            prefix = get_python_lib(1, 0)
        self.prefix = os.path.join(prefix, 'cppyy_backend')

    def run(self):
        # base run
        _build.run(self)

        # custom run
        log.info('Now building libcppyy_backend.so and dependencies')
        builddir = get_builddir()
        srcdir = get_srcdir()
        if not os.path.exists(builddir):
            log.info('Creating build directory %s ...' % builddir)
            os.makedirs(builddir)

        os.chdir(builddir)
        log.info('Running cmake for cppyy_backend')
        if subprocess.call([
                'cmake', srcdir, '-Dminimal=ON -Dasimage=OFF',
                '-DCMAKE_INSTALL_PREFIX='+self.prefix]) != 0:
            raise DistutilsSetupError('Failed to configure cppyy_backend')

        nprocs = os.getenv("MAKE_NPROCS")
        if nprocs:
            try:
                ival = int(nprocs)
                nprocs = '-j'+nprocs
            except ValueError:
                log.warn("Integer expected for MAKE_NPROCS, but got %s (ignored)", nprocs)
                nprocs = '-j1'
        else:
            nprocs = '-j1'
        log.info('Now building cppyy_backend and dependencies ...')
        if subprocess.call(['make', nprocs]) != 0:
            raise DistutilsSetupError('Failed to build cppyy_backend')

        log.info('build finished')

class my_libs_install(_install):
    def run(self):
        # base install
        _install.run(self)

        # custom install
        log.info('Now installing libcppyy_backend.so and dependencies')
        builddir = get_builddir()
        if not os.path.exists(builddir):
            raise DistutilsSetupError('Failed to find build dir!')
        os.chdir(builddir)

        prefix = self.install_lib
        log.info('Now installing in %s ...', prefix)
        if subprocess.call(['make', 'install']) != 0:
            raise DistutilsSetupError('Failed to install cppyy_backend')

        log.info('install finished')

    def get_outputs(self):
        outputs = _install.get_outputs(self)
        outputs.append(os.path.join(self.install_lib, 'cppyy_backend'))
        return outputs

setup(
    name='clingwrapper',
    description='C/C++ wrapper for Cling',
    long_description=long_description,
    url='http://pypy.org',

    # Author details
    author='PyPy Developers',
    author_email='pypy-dev@python.org',

    use_scm_version=True,
    setup_requires=['cppyy_backend'],

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

    keywords='interpreter development',

    packages=find_packages('src', ['backend']),
    include_package_data=True,

    cmdclass = {
        'build': my_cmake_build,
        'install': my_libs_install,
    },
)

