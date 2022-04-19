# Setup cppyy for development

## Installing the packages

### Install cling

Build cling with LLVM and clang:

```
git clone --depth=1 https://github.com/root-project/cling.git
git clone --depth=1 -b cling-llvm13 https://github.com/root-project/llvm-project.git
cd llvm-project
mkdir build
cd build
cmake -DLLVM_ENABLE_PROJECTS=clang                \
    -DLLVM_EXTERNAL_PROJECTS=cling                \
    -DLLVM_EXTERNAL_CLING_SOURCE_DIR=../../cling  \
    -DLLVM_TARGETS_TO_BUILD="host;NVPTX"          \
    -DCMAKE_BUILD_TYPE=Release                    \
    -DLLVM_ENABLE_ASSERTIONS=ON                   \
    -DLLVM_USE_LINKER=lld                         \
    -DCLANG_ENABLE_STATIC_ANALYZER=OFF            \
    -DCLANG_ENABLE_ARCMT=OFF                      \
    -DCLANG_ENABLE_FORMAT=OFF                     \
    -DCLANG_ENABLE_BOOTSTRAP=OFF                  \
    ../llvm
cmake --build . --target clang --parallel $(nproc --all)
cmake --build . --target cling --parallel $(nproc --all)
cmake --build . --target libcling --parallel $(nproc --all)
cmake --build . --target gtest_main --parallel $(nproc --all)
```

Note down the `llvm-project` directory location as we will need it later:
```
cd ../
export LLVM_DIR=$PWD
cd ../
```

### Install InterOp

Clone the InterOp repo. Build it using cling and install:

```
git clone https://github.com/compiler-research/InterOp.git
cd InterOp
mkdir build install && cd build
cmake -DUSE_CLING=ON -DCling_DIR=$LLVM_DIR/build -DCMAKE_INSTALL_PREFIX=$PWD/../install ..
cmake --build . --target install
```

Note down the path to InterOp install directory. This will be referred to as
`INTEROP_DIR`:

```
cd ../install
export INTEROP_DIR=$PWD
cd ../..
```

### Install cppyy-backend

Clone the repo, build it and copy library files into `python/cppyy-backend` directory:

```
git clone https://github.com/compiler-research/cppyy-backend.git
cd cppyy-backend
mkdir python/cppyy_backend/lib build && cd build
cmake -DInterOp_DIR=$INTEROP_DIR ..
cmake --build .
cp libcppyy-backend.so ../python/cppyy_backend/lib/
cp $LLVM_DIR/build/lib/libcling.so ../python/cppyy_backend/lib/
```

Note down the path to `cppyy-backend/python` directory as "CB_PYTHON_DIR":
```
cd ../python
export CB_PYTHON_DIR=$PWD
cd ../..
```

### Install CPyCppyy

Create virtual environment and activate it:
```
python3 -m venv .venv
source .venv/bin/activate
```

Clone the repo, checkout `rm-root-meta` branch and build it:
```
git clone https://github.com/compiler-research/CPyCppyy.git
cd CPyCppyy
mkdir build && cd build
cmake ..
cmake --build .
```

Note down the path to the `build` directory as `CPYCPPYY_DIR`:
```
export CPYCPPYY_DIR=$PWD
cd ../..
```

### Install cppyy

Clone the repo, checkout `rm-root-meta` branch and install:
```
git clone https://github.com/compiler-research/cppyy.git
cd cppyy
python -m pip install --upgrade . --no-deps
cd ..
```

## Run cppyy

Each time you want to run cppyy you need to:
1. Activate the virtual environment
    ```
    source .venv/bin/activate
    ```
2. Add `CPYCPPYY_DIR` and `CB_PYTHON_DIR` to `PYTHONPATH`:
    ```
    export PYTHONPATH=$PYTHONPATH:$CPYCPPYY_DIR:$CB_PYTHON_DIR
    ```
    The `CPYCPPYY_DIR` and `CB_PYTHON_DIR` will not be set if you start a new
    terminal instance so you can replace them with the actual path that they
    refer to.
3. Add paths to `CPLUS_INCLUDE_PATH`
    ```
    export CPLUS_INCLUDE_PATH="${CPLUS_INCLUDE_PATH}:${LLVM_DIR}/llvm/include:${LLVM_DIR}/clang/include:${LLVM_DIR}/build/include:${LLVM_DIR}/build/tools/clang/include"
    ```

Now you can `import cppyy` in `python`
```
python -c "import cppyy"
```

## Run the tests

**Follow the steps in Run cppyy.** Change to the test directory, make the library files and run pytest:
```
cd cppyy/test
make all
python -m pytest -sv
```
