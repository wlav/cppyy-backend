# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

# TInterpreter implementation for cling. Only in libCling; needs to resolve
# symbols from libCoreLegacy.

# Register the llvm include directories after clangs. This instructs the compiler to resolve
# headers from our builtin clang. That's an issue when we are building with bultin_llvm=Off
# and we have installed clang headers, too.
include_directories(SYSTEM
  ${CLANG_INCLUDE_DIRS}
  ${LLVM_INCLUDE_DIRS}
  ${CLAD_INCLUDE_DIRS}
)

include_directories(
  ../res
  ../../clingutils/res
  ../../foundation/res
  ${CLING_INCLUDE_DIRS}
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CLING_CXXFLAGS}")

if(MSVC)
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/TClingCallbacks.cxx COMPILE_FLAGS -GR-)
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/TClingRdictModuleFileExtension.cxx COMPILE_FLAGS -GR-)
else()
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/TClingCallbacks.cxx COMPILE_FLAGS -fno-rtti)
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/TClingRdictModuleFileExtension.cxx COMPILE_FLAGS -fno-rtti)
endif()

# This to avoid warning coming from llvm/src/tools/clang/include/clang/Sema/Lookup.h:441
# in clang + llvm version r227800.
if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  ROOT_ADD_CXX_FLAG(CMAKE_CXX_FLAGS -Wno-conditional-uninitialized)
endif()

# This is to avoid warnings coming from GCC 7 in llvm/src/include/llvm/ADT/DenseMap.h:1010
if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
  ROOT_ADD_CXX_FLAG(CMAKE_CXX_FLAGS -Wno-maybe-uninitialized)
endif()

ROOT_OBJECT_LIBRARY(MetaCling
  rootclingTCling.cxx
  TClingBaseClassInfo.cxx
  TClingCallbacks.cxx
  TClingCallFunc.cxx
  TClingClassInfo.cxx
  TCling.cxx
  TClingDataMemberInfo.cxx
  TClingDeclInfo.cxx
  TClingMethodArgInfo.cxx
  TClingMethodInfo.cxx
  TClingRdictModuleFileExtension.cxx
  TClingTypedefInfo.cxx
  TClingTypeInfo.cxx
  TClingValue.cxx
)

add_dependencies(MetaCling CLING)

##### libCling #############################################################

if(NOT builtin_clang)
  set(prefixed_link_libraries)
  foreach(dep ${CLING_DEPEND_LIBS})
    if("${dep}" MATCHES "^clang")
      set(dep "${LLVM_LIBRARY_DIR}/lib${dep}.a")
     endif()
     list(APPEND prefixed_link_libraries "${dep}")
   endforeach()
  set(LINK_LIBS "${prefixed_link_libraries}")
  link_directories("${LLVM_LIBRARY_DIR}")
endif()

ROOT_LINKER_LIBRARY(Cling
        $<TARGET_OBJECTS:ClingUtils>
        $<TARGET_OBJECTS:Dictgen>
        $<TARGET_OBJECTS:MetaCling>
        LIBRARIES ${CLING_LIBRARIES} ${LINK_LIBS} ${CLING_PLUGIN_LINK_LIBS})


if(MSVC)
  set_target_properties(Cling PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(cling_exports
      __std_terminate
      cling_runtime_internal_throwIfInvalidPointer
      ?kEmptyCollection@valuePrinterInternal@cling@@3QEBDEB
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PEBM@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PEBN@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PEBH@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PEBX@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PEB_N@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PEAPEBX@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PEAPEBD@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PEBV23@@Z
      ?setValueNoAlloc@internal@runtime@cling@@YAXPEAX00D@Z
      ?setValueNoAlloc@internal@runtime@cling@@YAXPEAX00DM@Z
      ?setValueNoAlloc@internal@runtime@cling@@YAXPEAX00DN@Z
      ?setValueNoAlloc@internal@runtime@cling@@YAXPEAX00DO@Z
      ?setValueNoAlloc@internal@runtime@cling@@YAXPEAX00DPEBX@Z
      ?setValueNoAlloc@internal@runtime@cling@@YAXPEAX00D_K@Z
      ?setValueWithAlloc@internal@runtime@cling@@YAPEAXPEAX00D@Z
      ??1Value@cling@@QEAA@XZ
      ??_7type_info@@6B@
      #?__type_info_root_node@@3U__type_info_node@@A
      ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@H@Z
      ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@M@Z
      ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@N@Z
      ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@PEBX@Z
      ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@P6AAEAV01@AEAV01@@Z@Z
      ??$?6U?$char_traits@D@std@@@std@@YAAEAV?$basic_ostream@DU?$char_traits@D@std@@@0@AEAV10@D@Z
      ??$?6U?$char_traits@D@std@@@std@@YAAEAV?$basic_ostream@DU?$char_traits@D@std@@@0@AEAV10@PEBD@Z
      ??$endl@DU?$char_traits@D@std@@@std@@YAAEAV?$basic_ostream@DU?$char_traits@D@std@@@0@AEAV10@@Z
      ??0LifetimeHandler@internal@runtime@cling@@QEAA@PEAVDynamicExprInfo@123@PEAVDeclContext@clang@@PEBDPEAVInterpreter@3@@Z
      ??1LifetimeHandler@internal@runtime@cling@@QEAA@XZ
      ?_Facet_Register@std@@YAXPEAV_Facet_base@1@@Z
      ??2@YAPEAX_K@Z
      ??3@YAXPEAX@Z
      ??3@YAXPEAX_K@Z
      #??2@YAPEAX_KPEAX@Z
      ??_U@YAPEAX_K@Z
      ??_V@YAXPEAX@Z
      memcpy
      memmove
      memset
      malloc
      calloc
      free
      ?EvaluateDynamicExpression@internal@runtime@cling@@YA?AVValue@3@PEAVInterpreter@3@PEAVDynamicExprInfo@123@PEAVDeclContext@clang@@@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PEBX@Z
    )
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(cling_exports
      __std_reverse_trivially_swappable_1
      __std_reverse_trivially_swappable_2
      __std_reverse_trivially_swappable_4
      __std_reverse_trivially_swappable_8
      __std_terminate
      ??2@YAPAXI@Z
      ??3@YAXPAX@Z
      ??3@YAXPAXI@Z
      ??_U@YAPAXI@Z
      ??_V@YAXPAX@Z
      ??_V@YAXPAXI@Z
      memcpy
      memmove
      memset
      malloc
      calloc
      free
      ??_7type_info@@6B@
      ?__type_info_root_node@@3U__type_info_node@@A
      ?nothrow@std@@3Unothrow_t@1@B
      cling_runtime_internal_throwIfInvalidPointer
      ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@H@Z
      ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@M@Z
      ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@N@Z
      ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@PBX@Z
      ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@P6AAAV01@AAV01@@Z@Z
      ??$?6U?$char_traits@D@std@@@std@@YAAAV?$basic_ostream@DU?$char_traits@D@std@@@0@AAV10@D@Z
      ??$?6U?$char_traits@D@std@@@std@@YAAAV?$basic_ostream@DU?$char_traits@D@std@@@0@AAV10@PBD@Z
      ??$endl@DU?$char_traits@D@std@@@std@@YAAAV?$basic_ostream@DU?$char_traits@D@std@@@0@AAV10@@Z
      ??0LifetimeHandler@internal@runtime@cling@@QAE@PAVDynamicExprInfo@123@PAVDeclContext@clang@@PBDPAVInterpreter@3@@Z
      ??1LifetimeHandler@internal@runtime@cling@@QAE@XZ
      ?_Facet_Register@std@@YAXPAV_Facet_base@1@@Z
      ??1Value@cling@@QAE@XZ
      ?isValid@Value@cling@@QBE_NXZ
      ?kEmptyCollection@valuePrinterInternal@cling@@3QBDB
      ?AssertOnUnsupportedTypeCast@Value@cling@@IBEXXZ
      ?setValueNoAlloc@internal@runtime@cling@@YAXPAX00D@Z
      ?setValueNoAlloc@internal@runtime@cling@@YAXPAX00DM@Z
      ?setValueNoAlloc@internal@runtime@cling@@YAXPAX00DN@Z
      ?setValueNoAlloc@internal@runtime@cling@@YAXPAX00DO@Z
      ?setValueNoAlloc@internal@runtime@cling@@YAXPAX00D_K@Z
      ?setValueNoAlloc@internal@runtime@cling@@YAXPAX00DPBX@Z
      ?setValueWithAlloc@internal@runtime@cling@@YAPAXPAX00D@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PBI@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PBM@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PBN@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PBH@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PBX@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PB_N@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PAPBX@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PAPBD@Z
      ?printValue@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PBV23@@Z
      ??$toUTF8@D@cling@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBDID@Z
      ?EvaluateDynamicExpression@internal@runtime@cling@@YA?AVValue@3@PAVInterpreter@3@PAVDynamicExprInfo@123@PAVDeclContext@clang@@@Z
    )
  endif()
  if(MSVC_VERSION LESS 1914)
    set(cling_exports ${cling_exports} ??3@YAXPAX0@Z ??_V@YAXPAX0@Z)
  endif()
  foreach(sym ${cling_exports})
    set(cling_link_str "${cling_link_str} /EXPORT:${sym}")
  endforeach(sym ${cling_exports})
  set_property(TARGET Cling APPEND_STRING PROPERTY LINK_FLAGS "${cling_link_str} ${CLAD_LIBS}")
  add_dependencies(Cling CoreLegacy RIOLegacy)
  target_link_libraries(Cling PUBLIC CoreLegacy RIOLegacy)
endif()

if(APPLE)
  target_link_libraries(Cling PUBLIC -Wl,-bind_at_load -Wl,-undefined -Wl,dynamic_lookup)
elseif(NOT MSVC)
  target_link_libraries(Cling PUBLIC -Wl,--unresolved-symbols=ignore-in-object-files)
endif()
