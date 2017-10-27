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
                namespaces[simplenames] = object
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
                    old_module = getattr(entity, "__module__", None)
                    if old_module is None:
                        setattr(parent, simplenames[-1], entity)
                    else:
                        new_module = ".".join([pkg_lib] + list(simplenames[:-1]))
                        #
                        # In theory, cppyy.gbl is formally an internal detail, so we could just steal the entity.
                        # Unfortunately, the "__module__" is not writeable, so we create a subclass instead.
                        #
                        # setattr(entity, "__module__", new_module)
                        # delattr(sys.modules[old_module], simplenames[-1])
                        # setattr(parent, simplenames[-1], entity)
                        #
                        # Clone the attributes, but set the __module__ correctly.
                        # TODO: nested classes should be replaced with our subclass.
                        #
                        attributes = {k: v for k,v in entity.__dict__.items()}
                        attributes["__module__"] = new_module
                        clazz = type(simplenames[-1], (entity, ), attributes)
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
