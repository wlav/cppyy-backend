"""
Support utilities for bindings.
"""
import os
import setuptools

import cppyy


def setup(pkg_dir, pkg_lib, cmake_shared_library_prefix, cmake_shared_library_suffix, pkg_version):
    """
    Wrap setuptools.setup for some bindings.

    :param pkg_dir:         Base directory of the bindings.
    :param pkg_lib:         Name of the bindings.
    :param cmake_shared_library_prefix:
                            ${cmake_shared_library_prefix}
    :param cmake_shared_library_suffix:
                            ${cmake_shared_library_suffix}
    :param pkg_version:     The version of the bindings.
    """
    pkg_file = cmake_shared_library_prefix + pkg_lib + cmake_shared_library_suffix
    setuptools.setup(
        name=pkg_lib,
        version=pkg_version,
        description='Bindings for ' + pkg_lib,
        author=os.environ["USER"],
        package_data={'': [pkg_file, pkg_lib + '.rootmap', pkg_lib + '_rdict.pcm']},
        py_modules=[pkg_lib],
        packages=[''],
    )
