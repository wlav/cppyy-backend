"""
Support utilities for bindings.
"""
from distutils import log
from distutils.command.clean import clean
from distutils.util import get_platform
from setuptools.command.build_py import build_py
from wheel.bdist_wheel import bdist_wheel
import os
import re
import setuptools
import subprocess
import sys

import cppyy


PRIMITIVE_TYPES = re.compile(r"\b(bool|char|short|int|unsigned|long|float|double)\b")


def rootmapper(pkg_file, cmake_shared_library_prefix, cmake_shared_library_suffix, pkg_namespace=""):
    """
    Populate a module with some rootmap'ped bindings.

    :param pkg_file:        Base Python file of the bindings.
    :param cmake_shared_library_prefix:
                            ${cmake_shared_library_prefix}
    :param cmake_shared_library_suffix:
                            ${cmake_shared_library_suffix}
    :param pkg_namespace:   Any namespace for the bindings.
    """
    def add_to_pkg(keyword, simplenames):
        #
        # Add level 1 objects to the pkg namespace.
        #
        if len(simplenames) > 1:
            return
        #
        # Ignore some names based on heuristics.
        #
        simplename = simplenames[0]
        if simplename in ('void'):
            return
        if PRIMITIVE_TYPES.search(simplename):
            return
        #
        # Classes, variables etc.
        #
        entity = getattr(cppyy.gbl, simplename)
        if getattr(entity, "__module__", None) == "cppyy.gbl":
            setattr(entity, "__module__", pkg)
        setattr(pkg_module, simplename, entity)

    pkg_dir, pkg_py = os.path.split(pkg_file)
    pkg_simplename = os.path.basename(pkg_dir)
    if pkg_namespace:
        pkg = pkg_namespace + "." + pkg_simplename
    else:
        pkg = pkg_simplename
    pkg_module = sys.modules[pkg]
    lib_file = cmake_shared_library_prefix + pkg_simplename + cmake_shared_library_suffix
    #
    # Load the library.
    #
    cppyy.load_reflection_info(os.path.join(pkg_dir, lib_file))
    #
    # Parse the rootmap file.
    #
    rootmap = os.path.join(pkg_dir, pkg_simplename + ".rootmap")
    with open(rootmap, 'rU') as rootmap:
        #
        # "decls" part.
        #
        for line in rootmap:
            if line.startswith('['):
                #
                # We found the start of the [pkg_simplename].
                #
                break
            names = re.sub("[{};]", "", line).split()
            if not names or not names[0] in ('namespace'):
                continue
            keys = range(0, len(names) - 1, 2)
            keyword = [name for i, name in enumerate(names) if i in keys][-1]
            simplenames = [name for i, name in enumerate(names) if i not in keys]
            add_to_pkg(keyword, simplenames)
        #
        # [pkg_simplename] part.
        #
        for line in rootmap:
            line = line.strip().split(None, 1)
            if len(line) < 2:
                continue
            keyword, names = line
            if not keyword in ('class', 'var', 'namespace', 'typedef'):
                continue
            names = re.split("[<>(),\s]+", names)
            names = [name for name in names if name]
            for name in names:
                simplenames = name.split('::')
                add_to_pkg(keyword, simplenames)


def setup(pkg_dir, pkg, cmake_shared_library_prefix, cmake_shared_library_suffix, pkg_version, author,
          author_email, url, license):
    """
    Wrap setuptools.setup for some bindings.

    :param pkg_dir:         Base directory of the bindings.
    :param pkg:             Name of the bindings.
    :param cmake_shared_library_prefix:
                            ${cmake_shared_library_prefix}
    :param cmake_shared_library_suffix:
                            ${cmake_shared_library_suffix}
    :param pkg_version:     The version of the bindings.
    :param author:          The name of the library author.
    :param author_email:    The email address of the library author.
    :param url:             The home page for the library.
    :param license:         The license.
    """
    pkg_simplename = re.findall("[^\.]+$", pkg)[0]
    pkg_namespace = re.sub("\.?" + pkg_simplename, "", pkg)
    pkg_dir = os.path.join(pkg_dir, pkg.replace(".", os.path.sep))
    pkg_file = cmake_shared_library_prefix + pkg_simplename + cmake_shared_library_suffix
    long_description = """Bindings for {}.
These bindings are based on https://cppyy.readthedocs.io/en/latest/, and can be
used as per the documentation provided via the cppyy.cgl namespace. The environment
variable LD_LIBRARY_PATH must contain the path of the {}.rootmap file. Use
"import cppyy; from cppyy.gbl import <some-C++-entity>".

Alternatively, use "import {}". This convenience wrapper supports "discovery" of the
available C++ entities using, for example Python 3's command line completion support.
""".replace("{}", pkg)

    class my_build_py(build_py):
        def run(self):
            #
            # Base build.
            #
            build_py.run(self)
            #
            # Custom build.
            #
            cmd = ["make"]
            if self.dry_run:
                cmd += ["--dry-run"]
            if self.verbose:
                cmd += ["VERBOSE=1"]
            subprocess.check_call(cmd)
            #
            # Move CMake output to self.build_lib.
            #
            pkg_subdir = pkg.replace(".", os.path.sep)
            if pkg_namespace:
                #
                # Implement a pkgutil-style namespace package as per the guidance on
                # https://packaging.python.org/guides/packaging-namespace-packages.
                #
                namespace_init = os.path.join(pkg_namespace, "__init__.py")
                with open(namespace_init, "w") as f:
                    f.write("__path__ = __import__('pkgutil').extend_path(__path__, __name__)\n")
                self.copy_file(namespace_init, os.path.join(self.build_lib, namespace_init))
            for f in self.package_data[pkg]:
                self.copy_file(os.path.join(pkg_subdir, f), os.path.join(self.build_lib, pkg_subdir, f))

    class my_clean(clean):
        def run(self):
            #
            # Custom clean.
            # TODO: There is no way to reliably clean the "dist" directory.
            #
            cmd = ["make", "clean"]
            if self.dry_run:
                cmd += ["--dry-run"]
            if self.verbose:
                cmd += ["VERBOSE=1"]
            subprocess.check_call(cmd)
            #
            #  Base clean.
            #
            clean.run(self)

    class my_bdist_wheel(bdist_wheel):
        def finalize_options(self):
            #
            # This is a universal (Python2/Python3), but platform-specific (has
            # compiled parts) package; a combination that wheel does not recognize,
            # thus simply fool it.
            #
            self.plat_name = get_platform()
            bdist_wheel.finalize_options(self)
            self.root_is_pure = True

    setuptools.setup(
        name=pkg,
        version=pkg_version,
        author=author,
        author_email=author_email,
        url=url,
        license=license,
        description='Bindings for ' + pkg,
        long_description=long_description,
        platforms=['any'],
        package_data={pkg: [pkg_file, pkg_simplename + '.rootmap', pkg_simplename + '_rdict.pcm']},
        packages=[pkg],
        zip_safe=False,
        cmdclass={
            'build_py': my_build_py,
            'clean': my_clean,
            'bdist_wheel': my_bdist_wheel,
        },
    )
