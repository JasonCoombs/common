#
#
# ***********************************************************************************
# * Copyright (C) 2019 - 2020, BlockSettle AB
# * Distributed under the GNU Affero General Public License (AGPL v3)
# * See LICENSE or http://www.gnu.org/licenses/agpl.html
# *
# **********************************************************************************
#
#
include(CheckCCompilerFlag)

if(CMAKE_C_COMPILER_ID STREQUAL GNU)
   unset(COMPILER_COLOR_DIAGNOSTICS)
   check_c_compiler_flag(-fdiagnostics-color=always COMPILER_COLOR_DIAGNOSTICS)
   if(COMPILER_COLOR_DIAGNOSTICS)
      add_compile_options(-fdiagnostics-color=always)
   else()
      unset(COMPILER_COLOR_DIAGNOSTICS)
      check_c_compiler_flag(-fdiagnostics-color COMPILER_COLOR_DIAGNOSTICS)
      if(COMPILER_COLOR_DIAGNOSTICS)
	 add_compile_options(-fdiagnostics-color)
      endif()
   endif()
elseif(CMAKE_C_COMPILER_ID STREQUAL Clang)
   unset(COMPILER_COLOR_DIAGNOSTICS)
   check_c_compiler_flag(-fcolor-diagnostics COMPILER_COLOR_DIAGNOSTICS)
   if(COMPILER_COLOR_DIAGNOSTICS)
      add_compile_options(-fcolor-diagnostics)
   endif()
endif()
