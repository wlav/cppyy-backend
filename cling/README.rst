cppyy-cling
===========

A repackaging of Cling, the interactive C++ interpreter, including a version
of LLVM that is patched for interactive use.

Compilation of LLVM may take a long time, so when building from source, it is
recommended to set MAKE_NPROCS to the number of cores on your machine and to
use the verbose flag to see progress:

  $ MAKE_NPROCS=32 pip install --verbose cppyy-cling

Cling documentation is here:
  https://root.cern.ch/cling

----

Find the cppyy documentation here:
  http://cppyy.readthedocs.io/
