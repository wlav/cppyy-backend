"""
Support utilities for bindings.
"""
import os
import setuptools
import sys

import cppyy


def rootmapper(py_file, cmake_shared_library_prefix, cmake_shared_library_suffix):
    """
    Populate a module with some rootmap'ped bindings.

    :param py_file:         Base Python file of the bindings.
    :param cmake_shared_library_prefix:
                            ${cmake_shared_library_prefix}
    :param cmake_shared_library_suffix:
                            ${cmake_shared_library_suffix}
    """
    pkg_dir, pkg_py = os.path.split(py_file)
    pkg_lib = os.path.splitext(pkg_py)[0]
    pkg_module = sys.modules[pkg_lib]
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
            if keyword in ('class', 'var', 'namespace'):
                #
                # Classes, variables etc.
                #
                entity = getattr(cppyy.gbl, name)
                #
                # In theory, cppyy.gbl is formally an internal detail, so we could just steal the entity:
                #
                # setattr(entity, "__module__", new_module)
                # delattr(sys.modules[old_module], simplenames[-1])
                # setattr(parent, simplenames[-1], entity)
                #
                # Unfortunately, the "__module__" is not writeable, and creating a subclass instead:
                #
                # attributes = {k: v for k, v in entity.__dict__.items()}
                # attributes["__module__"] = new_module
                # objects[simplenames] = type(simplenames[-1], (entity, ), attributes)
                #
                # just introduces confusion. It seems all we can do is to synthesise namespaces,
                # and just insert classes etc. as-is.
                #
                if keyword == "namespace":
                    new_module = ".".join([pkg_lib] + list(simplenames[:-1]))
                    objects[simplenames] = type(simplenames[-1], (object,), {"__module__": new_module})
                else:
                    objects[simplenames] = entity
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
                    setattr(parent, simplenames[-1], entity)


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
    long_description = """Bindings for {}.
These bindings are based on https://cppyy.readthedocs.io/en/latest/, and can be
used as per the documentation provided via the cppyy.cgl namespace. The environment
variable LD_LIBRARY_PATH must contain the path of the {}.rootmap file.

Alternatively, use "import {}". This convenience wrapper supports "discovery" of the
bindings using, for example Python 3's command line completion support.
""".replace("{}", pkg_lib)
    setuptools.setup(
        name=pkg_lib,
        version=pkg_version,
        description='Bindings for ' + pkg_lib,
        long_description=long_description,
        platforms=['any'],
        package_data={'': [pkg_file, pkg_lib + '.rootmap', pkg_lib + '_rdict.pcm']},
        py_modules=[pkg_lib],
        packages=[''],
        zip_safe=False,
    )
