"""
Support utilities for bindings.
"""
from distutils import log
from distutils.command.clean import clean
from setuptools.command.build_py import build_py
import os
import re
import setuptools
import subprocess
import sys

import cppyy


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
    def lookup(keyword, name):
        simplenames = tuple(name.split('::'))
        if keyword in ('class', 'var', 'namespace'):
            #
            # Classes, variables etc.
            #
            try:
                entity = getattr(cppyy.gbl, name)
            except AttributeError as e:
                log.warn("Unable to look up item %s", e)
            else:
                log.warn("Looked up item %s", str(entity))
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
                    new_module = ".".join([pkg_simplename] + list(simplenames[:-1]))
                    objects[simplenames] = type(simplenames[-1], (object,), {"__module__": new_module})
                else:
                    objects[simplenames] = entity

    pkg_dir, pkg_py = os.path.split(pkg_file)
    pkg_simplename = os.path.basename(pkg_dir)
    pkg_module = sys.modules[pkg_namespace or pkg_simplename]
    lib_file = cmake_shared_library_prefix + pkg_simplename + cmake_shared_library_suffix
    #
    # Load the library.
    #
    cppyy.load_reflection_info(os.path.join(pkg_dir, lib_file))
    #
    # Parse the rootmap file.
    #
    namespaces = {}
    objects = {}
    with open(os.path.join(pkg_dir, pkg_simplename + '.rootmap'), 'rU') as rootmap:
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
            names = [name for i, name in enumerate(names) if i not in keys]
            name = "::".join(names)
            lookup(keyword, name)
        #
        # [pkg_simplename] part.
        #
        for line in rootmap:
            line = line.strip().split(None, 1)
            if len(line) < 2:
                continue
            keyword, names = line
            if not keyword in ('class', 'var', 'namespace'):
                continue
            names = re.split("[<>(),\s]+", names)
            names = [name for name in names if name]
            for name in names:
                lookup(keyword, name)
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
variable LD_LIBRARY_PATH must contain the path of the {}.rootmap file.

Alternatively, use "import {}". This convenience wrapper supports "discovery" of the
bindings using, for example Python 3's command line completion support.
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
            if self.verbose:
                cmd += ["VERBOSE=1"]
            subprocess.check_call(cmd)
            #
            # Move CMake output to self.build_lib.
            #
            pkg_subdir = pkg.replace(".", os.path.sep)
            pkg_data = list(self.package_data[pkg])
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
            #
            cmd = ["make", "clean"]
            if self.verbose:
                cmd += ["VERBOSE=1"]
            subprocess.check_call(cmd)
            #
            #  Base clean.
            #
            clean.run(self)

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
        cmdclass = {
            'build_py': my_build_py,
            'clean': my_clean,
        },
    )
