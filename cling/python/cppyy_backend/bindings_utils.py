"""
Support utilities for bindings.
"""
import os
import setuptools

import cppyy


def rootmapper(pkg_dir, pkg_lib, cmake_shared_library_prefix, cmake_shared_library_suffix, pkg_module):
    """
    Populate a module with some rootmap'ped bindings.

    :param pkg_dir:         Base directory of the bindings.
    :param pkg_lib:         Name of the bindings.
    :param cmake_shared_library_prefix:
                            ${cmake_shared_library_prefix}
    :param cmake_shared_library_suffix:
                            ${cmake_shared_library_suffix}
    :param pkg_module:      The module to be populated.
    """
    pkg_file = cmake_shared_library_prefix + pkg_lib + cmake_shared_library_suffix
    #
    # Load the library.
    #
    cppyy.load_reflection_info(os.path.join(pkg_dir, pkg_file))
    #
    # Parse the rootmap file.
    #
    namespaces = {}
    objects = {}
    with open(os.path.join(pkg_dir, pkg_lib + '.rootmap'), 'rU') as rootmap:
        for line in rootmap:
            if line.startswith('['):
                #
                # We found the start of the section we care about.
                #
                break
        for line in rootmap:
            line = line.strip().split(None, 1)
            if len(line) < 2:
                continue
            keyword, name = line
            simplenames = tuple(name.split('::'))
            if keyword == 'namespace':
                namespaces[simplenames] = getattr(cppyy.gbl, name)
            elif keyword in ('class', 'var'):
                #
                # Classes, variables etc.
                #
                objects[simplenames] = getattr(cppyy.gbl, name)
        #
        # Set up namespaces, then other objects, in depth order.
        #
        for entities in [namespaces, objects]:
            levels = list({len(k) for k in entities.keys()})
            levels.sort()
            for level in levels:
                names_at_level = [k for k in entities.keys() if len(k) == level]
                for simplenames in names_at_level:
                    parent = pkg_module
                    for prefix in simplenames[:-1]:
                        try:
                            parent = getattr(parent, prefix)
                        except AttributeError:
                            parent = parent[prefix]
                    entity = entities[simplenames]
                    #
                    # Properties can simply be added to the parent, classes however can only have one parent.
                    #
                    if not hasattr(entity, "__name__"):
                        setattr(parent, simplenames[-1], entity)
                    else:
                        if entities is namespaces:
                            base_clazzes = tuple()
                        else:
                            base_clazzes = (entity,)
                        clazz = type(simplenames[-1], base_clazzes, {"__module__": parent.__name__})
                        setattr(parent, simplenames[-1], clazz)


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
