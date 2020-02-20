#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
#   Copyright (C) Golaem S.A.  All Rights Reserved.
#
#   dev@golaem.com
#
#-----------------------------------------------------------------------------------------------------------------------------------------------------

# This file provides CMake variables & macros commonly used in Golaem projects.
#
# The following variables are available :
# - GOLAEM_MODULE_PATH ................ Path to folder containing Golaem macros file
# - BUILD_COMPILER_VERSION ............ Current MSVC/GCC compiler version (empty if not a supported MSVC/GCC compiler)
# - BUILD_ARCHITECTURE_VERSION ........ Current architecture version ("x86" or "x64")
#
# The following macros are available :
# - SETUP_PROJECT_CONFIGURATION ....... Set up the default configuration for Golaem projects
# - SET_ENVIRONMENT_VARIABLE .......... Set a WIN32 environment variable
# - GET_RELEASE_LABEL ................. Generate clean release label from release numbers
# - GENERATE_UNITY_BUILD .............. Generates a single cpp file including all sources, to speed up compile times, look up Unity build
# - GET_DEBUG_LIBRARIES ............... Extract debug libraries contained in a list of libraries (i.e. any library preceded by the "debug" keyword or not precessed by any keyword)
# - GET_OPTIMIZED_LIBRARIES ........... Extract optimized libraries contained in a list of libraries (i.e. any library preceded by the "optimized" keyword or not precessed by any keyword)
# - DEDUCE_LIBRARY_FILENAME ........... Deduce the full name of a library (append postfix, add extension according to platform ...)
# - GET_LINK_LIBRARY_NAMES ............ Generate the appropriate post-fixed library name for link (according the available compilers and configurations)
# - SET_TARGET_LIBRARY_POSTFIX ........ Set the appropriate postfix to a library target (corresponding to the MSVC version and the debug/release configuration used)
# - SET_TARGET_EXECUTABLE_POSTFIX ..... Set the appropriate postfix to an executable target (corresponding to the debug/release configuration used only)
# - SET_TARGET_LIBRARY_CONFIG_STATIC .. Configure static library according to target platform
# - SET_TARGET_LIBRARY_CONFIG_SHARED .. Configure shared library according to target platform
# - SET_TARGET_EXECUTABLE_CONFIG ...... Configure executable according to target platform
# - LIST_FILES ........................ List & group files located in a given directory
# - ADD_PROJECT_OPTION ................ Add a build option to the current project
# - ADD_OPTION_DEPENDENCY ............. Add a dependency to an existing build option
# - ADD_OPTIONS_DEPENDENCY ............ Specify a dependency to a set of existing build option
# - FIND_DEPENDENCY ................... Find a dependency by calling related find script script (located in CMAKE_MODULE_PATH, GOLAEM_MODULE_PATH or any of the specified folders)
# - CHECK_BUILD_OPTION ................ Check that an option is buildable (i.e. this build option is set to ON and none of its dependencies is missing)
# - REPORT_MESSAGE .................... Add a message to report
# - PRINT_REPORT ...................... Print reported errors/warnings/status information report


#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro REPORT_MESSAGE - used in GolaemDefinitions.cmake
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Add a message to report.
#
# Input parameters :
# - REPORT_TYPE : type of message to report (must be ERROR, WARNING or INFO)
# - REPORT_MSG : message to report
#
# Examples :
#   report_message( "ERROR" "Error message to report" )

macro( report_message REPORT_TYPE REPORT_MSG )

	if( ( "${REPORT_TYPE}" STREQUAL "ERROR" ) OR ( "${REPORT_TYPE}" STREQUAL "WARNING" ) OR ( "${REPORT_TYPE}" STREQUAL "INFO" ) )
		if( ( "${REPORT_${REPORT_TYPE}}" STREQUAL "" ) OR ( NOT REPORT_${REPORT_TYPE} ) )
			set( REPORT_${REPORT_TYPE} "${REPORT_MSG}" CACHE "Report message (level '${REPORT_TYPE}')" INTERNAL FORCE )
		else()
			set( REPORT_${REPORT_TYPE} "${REPORT_${REPORT_TYPE}};${REPORT_MSG}" CACHE "Report message (level '${REPORT_TYPE}')" INTERNAL FORCE )
		endif()
	else()
		report_message( "WARNING" "Call to macro 'REPORT_MESSAGE' with incorrect report type '${REPORT_TYPE}'" )
	endif()

endmacro()

#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Variable GOLAEM_MODULE_PATH + include Golaem build definitions
#-----------------------------------------------------------------------------------------------------------------------------------------------------

set( GOLAEM_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}" )
include( ${GOLAEM_MODULE_PATH}/GolaemDefinitions.cmake )


#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Variable BUILD_COMPILER_VERSION
#-----------------------------------------------------------------------------------------------------------------------------------------------------
if( NOT BUILD_COMPILER_VERSION )
	if( MSVC )
		# include( CMakeDetermineVSServicePack )
		# DetermineVSServicePack( SERVICEPACK_VERSION )
		set( BUILD_COMPILER_VERSION "vc${MSVC_TOOLSET_VERSION}")
		if( MSVC_TOOLSET_VERSION EQUAL 141)
			set( BUILD_COMPILER_VERSION "vc140" )
		endif()
	elseif( CMAKE_COMPILER_IS_GNUCC )
		execute_process( COMMAND ${CMAKE_C_COMPILER} -dumpversion OUTPUT_VARIABLE GCC_VERSION )
		string( REGEX MATCHALL "[0-9]+" GCC_VERSION_NUMBERS ${GCC_VERSION} )
		set( BUILD_COMPILER_VERSION "gcc" )
		foreach( number ${GCC_VERSION_NUMBERS} )
			set( BUILD_COMPILER_VERSION "${BUILD_COMPILER_VERSION}${number}" )
		endforeach()
	elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
		execute_process( COMMAND ${CMAKE_CXX_COMPILER} --version OUTPUT_VARIABLE clang_full_version_string )
		string( REGEX MATCHALL "[0-9]+" CLANG_VERSION_NUMBERS ${clang_full_version_string} )
		LIST(GET CLANG_VERSION_NUMBERS 0 major)
		LIST(GET CLANG_VERSION_NUMBERS 1 minor)
		LIST(GET CLANG_VERSION_NUMBERS 2 revision)
		set( BUILD_COMPILER_VERSION "Clang${major}${minor}${revision}" )
	else()
		set( BUILD_COMPILER_VERSION "" )
	endif()
endif()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Variable BUILD_ARCHITECTURE_VERSION
#-----------------------------------------------------------------------------------------------------------------------------------------------------

if( NOT BUILD_ARCHITECTURE_VERSION )
	if( MSVC )
		if( CMAKE_CL_64 )
			set( BUILD_ARCHITECTURE_VERSION "x64" )
		else()
			set( BUILD_ARCHITECTURE_VERSION "x86" )
		endif()
	elseif( CMAKE_COMPILER_IS_GNUCC )
		if( CMAKE_SYSTEM_PROCESSOR MATCHES x86_64 )
			set( BUILD_ARCHITECTURE_VERSION "x64" )
		elseif( CMAKE_SYSTEM_PROCESSOR MATCHES i686 )
			set( BUILD_ARCHITECTURE_VERSION "x86" )
		elseif( CMAKE_SYSTEM_PROCESSOR MATCHES x86 )
			set( BUILD_ARCHITECTURE_VERSION "x86" )
		endif()
	elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
		set( BUILD_ARCHITECTURE_VERSION "x64" )
	else()
		set( BUILD_ARCHITECTURE_VERSION "" )
	endif()
endif()




#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro SETUP_PROJECT_CONFIGURATION
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Set up the default configuration for Golaem projects, and particularly set the following variables :
#   - LIBRARY_OUTPUT_DIRECTORY ;
#   - RUNTIME_OUTPUT_DIRECTORY ;
#   - CMAKE_INSTALL_PREFIX.
#
# Optional parameters :
# - ENVIRONMENT_VARIABLE_NAME = Name of the environment variable associated to the project (e.g. ${GLM_SDK_HOME})
# - ENVIRONMENT_VARIABLE_PATH = Relative path to append to 'CMAKE_INSTALL_PREFIX' that will be stored in environment variable
#
# Example :
#   setup_project_configuration()
#   setup_project_configuration( "GLM_SDK_HOME" "sdk" )
#   setup_project_configuration( "GLM_CROWD_HOME" )

macro( setup_project_configuration )

	# CMake policy configuration
	set( CMAKE_COLOR_MAKEFILE ON )
	set( CMAKE_VERBOSE_MAKEFILE 1 )
	# cmake_policy( SET CMP0003 OLD )
	# cmake_policy( SET CMP0010 OLD )

	# Define configuration types
	set( CMAKE_CONFIGURATION_TYPES
		Debug
		Release
		RelWithDebInfo
	)

	# Make "Release" as default build under UNIX systems
	if( UNIX AND NOT CMAKE_BUILD_TYPE )
		set( CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are : Debug or Release." FORCE )
	endif()

	# Set output directories
	set( GOLAEM_BINARIES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin" CACHE PATH "Directory where binaries (libs and executables) are built" )
	if( ( ${ARGC} GREATER 0 ) AND ( NOT "$ENV{${ARGV0}}" STREQUAL "" ) )
		set( GOLAEM_INSTALL_DIR "$ENV{${ARGV0}}" CACHE PATH "Directory where files (libs, headers...) are installed" )
	else()
		set( GOLAEM_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/pack" CACHE PATH "Directory where files (libs, headers...) are installed" )
	endif()
	set( CMAKE_INSTALL_PREFIX "${GOLAEM_INSTALL_DIR}" CACHE INTERNAL "Install path prefix, prepended onto install directories" )
	if( NOT MSVC )
		set( EXTRA_OUTPUT_PATH "/${CMAKE_BUILD_TYPE}" )
	endif()
	set( EXECUTABLE_OUTPUT_PATH "${GOLAEM_BINARIES_DIR}${EXTRA_OUTPUT_PATH}" CACHE INTERNAL "Directory where to build libs" )
	set( LIBRARY_OUTPUT_PATH "${GOLAEM_BINARIES_DIR}${EXTRA_OUTPUT_PATH}" CACHE INTERNAL "Directory where to build executables")
	set( RUNTIME_OUTPUT_DIRECTORY "${GOLAEM_BINARIES_DIR}${EXTRA_OUTPUT_PATH}" CACHE INTERNAL "Directory where to build libs" )
	set( LIBRARY_OUTPUT_DIRECTORY "${GOLAEM_BINARIES_DIR}${EXTRA_OUTPUT_PATH}" CACHE INTERNAL "Directory where to build executables" )
	set( ARCHIVE_OUTPUT_DIRECTORY "${GOLAEM_BINARIES_DIR}${EXTRA_OUTPUT_PATH}" CACHE INTERNAL "Directory where to build executables" )
	file( TO_CMAKE_PATH ${EXECUTABLE_OUTPUT_PATH} EXECUTABLE_OUTPUT_PATH )
	file( TO_CMAKE_PATH ${LIBRARY_OUTPUT_PATH} LIBRARY_OUTPUT_PATH )
	file( TO_CMAKE_PATH ${RUNTIME_OUTPUT_DIRECTORY} RUNTIME_OUTPUT_DIRECTORY )
	file( TO_CMAKE_PATH ${LIBRARY_OUTPUT_DIRECTORY} LIBRARY_OUTPUT_DIRECTORY )
	file( TO_CMAKE_PATH ${ARCHIVE_OUTPUT_DIRECTORY} ARCHIVE_OUTPUT_DIRECTORY )

	# Set CMAKE_INSTALL_PREFIX and ENVIRONMENT_VARIABLE_NAME values
	if( ( ${ARGC} GREATER 0 ) AND ( NOT "${ARGV0}" STREQUAL "" ) )
		if( ( ${ARGC} GREATER 1 ) AND ( NOT "${ARGV1}" STREQUAL "" ) )
			set_environment_variable( ${ARGV0} "${CMAKE_INSTALL_PREFIX}/${ARGV1}" )
		else()
			set_environment_variable( ${ARGV0} "${CMAKE_INSTALL_PREFIX}" )
		endif()
	endif()
	
	# Option for using unity builds
	option( GOLAEM_USE_UNITY_BUILDS "Use unity builds (defaultly use unity builds)" ON )
	if( GOLAEM_USE_UNITY_BUILDS )
		set( GOLAEM_USE_UNITY_BUILDS_FILESCOUNT "4" CACHE STRING "Set count of unity build files to use (default is 4)." )
		report_message( "INFO" "Use unity builds (setup for ${GOLAEM_USE_UNITY_BUILDS_FILESCOUNT} unity build files)" )
	else()
		unset( GOLAEM_USE_UNITY_BUILDS_FILESCOUNT CACHE )
		report_message( "INFO" "Do NOT use unity builds" )
	endif()

	# Option for using/building static libraries (by default use/build dynamic libraries)
	option( GOLAEM_USE_STATIC_LIBRARIES "Use and build static libraries (defaultly use/build dynamic libraries)" ON )
	option( GOLAEM_USE_STATIC_CORE_LIBRARY "Use and build static glmCore library (defaultly use/build dynamic libraries)" OFF )
	
	if( GOLAEM_USE_STATIC_CORE_LIBRARY )
		set( GOLAEM_USE_STATIC_LIBRARIES ON CACHE BOOL "Use and build static libraries (defaultly use/build dynamic libraries)" FORCE )
		report_message( "INFO" "Use Golaem STATIC libraries (including glmCore library)" )
	else()
		if( GOLAEM_USE_STATIC_LIBRARIES )
			report_message( "INFO" "Use Golaem STATIC libraries (except glmCore library as DYNAMIC)" )
		else ()
			report_message( "INFO" "Use Golaem DYNAMIC libraries (including glmCore library)" )
		endif()
	endif()
	
	option( GOLAEM_USE_MEMORY_ALLOC_OVERRIDE "Use Golaem Memory Allocator Override (Enable Golaem Memory Tracking)" OFF )
	option( GOLAEM_USE_SHAPE_SUBSCENE "Use subscene for cache proxy & entitytypeNode(default is override)" OFF )
	
endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro SET_ENVIRONMENT_VARIABLE
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Set a WIN32 environment variable.
#
# Input parameters :
# - VARIABLE_NAME = Name of the environment variable to set  (e.g. ${GLM_SDK_HOME})
# - VARIABLE_PATH = Path contained in the environment variable
#
# Example :
#   set_environment_variable( "GLM_SDK_HOME" "${CMAKE_INSTALL_PREFIX}" )

function( set_environment_variable VARIABLE_NAME VARIABLE_PATH )

	if( WIN32 )
		set( BAT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/SetEnvironment.bat" )
		file( TO_NATIVE_PATH ${VARIABLE_PATH} VARIABLE_PATH )
		file( WRITE "${BAT_FILE}" "REG ADD HKCU\\Environment /f /v ${VARIABLE_NAME} /d \"${VARIABLE_PATH}\"" )  
		execute_process( COMMAND "${BAT_FILE}" )
		file( REMOVE "${BAT_FILE}" )
	endif()

endfunction()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro GET_RELEASE_LABEL
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Generate clean release label from release numbers.
#
# Input parameters :
# - RELEASE_RAW = Raw string containing release label
#
# Output parameters :
# - RELEASE_CLEAN = Variable containing the resulting unity build files
#
# Example :
#   get_release_label( "${GLM_SDK_MAJORVERSION}.${GLM_SDK_MINORVERSION}.${GLM_SDK_PATCHVERSION}.${GLM_SDK_BRANCHVERSION}" GLM_SDK_RELEASE_LABEL )

macro( get_release_label RELEASE_RAW RELEASE_CLEAN )

	set( string_parsed "${RELEASE_RAW}" )
	string( LENGTH "${string_parsed}" string_length )
	math( EXPR string_index "${string_length} - 2" )
	while( ( "${string_index}" GREATER "0" ) )
		string( SUBSTRING "${string_parsed}" "${string_index}" "2" string_to_remove )
		if( "${string_to_remove}" STREQUAL ".0" )
			string( SUBSTRING "${string_parsed}" "0" "${string_index}" string_parsed )
			set( string_length ${string_index} )
			math( EXPR string_index "${string_length} - 2" )
		else()
			set( string_index 0 )
		endif()
	endwhile()
	set( ${RELEASE_CLEAN} "${string_parsed}" )

endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro CONFIGURE_UNITY_BUILD
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Configure current target to use unity build (to speed up compile time by generating a restricted set off .cpp files)
#
# Input parameters :
# - TARGET_NAME = Name of the target
# - SOURCE_FILES = List the source files to include
#
# Example :
#	configure_unity_build( "${TARGET_CORE}" CORE_ALLFILES )
#
# Tips :
# - Macro must be called BEFORE adding source files to project
# - Macro is responsible for checking if GOLAEM_USE_UNITY_BUILDS is enabled 

macro( configure_unity_build TARGET_NAME SOURCE_FILES )

	if( GOLAEM_USE_UNITY_BUILDS )
		file( GLOB previous_UNITY_BUILD_FILES ${CMAKE_CURRENT_BINARY_DIR}/*UnityBuild*.c* )
		if( previous_UNITY_BUILD_FILES )
			file( REMOVE ${previous_UNITY_BUILD_FILES} )
		endif()

		set( UNITY_BUILD_FILE_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}UnityBuild" )
		set( local_UNITY_BUILD_FILES "" )

		if( NOT GOLAEM_USE_UNITY_BUILDS_FILESCOUNT )
			set( GOLAEM_USE_UNITY_BUILDS_FILESCOUNT "8" )
		endif()
		
		set( unitybuild_file_count "0" )
		while( unitybuild_file_count LESS GOLAEM_USE_UNITY_BUILDS_FILESCOUNT )
			set( current_UNITY_BUILD_FILE "${UNITY_BUILD_FILE_PREFIX}_${unitybuild_file_count}.cpp" )
			set( local_UNITY_BUILD_FILES "${local_UNITY_BUILD_FILES}" "${current_UNITY_BUILD_FILE}" )
			file( WRITE ${current_UNITY_BUILD_FILE} "// Unity build file generated by CMake\n" )
			math( EXPR unitybuild_file_count "${unitybuild_file_count} + 1" )
		endwhile()
		
		set( unity_build_c_files OFF )
		foreach( source_file ${${SOURCE_FILES}} )
			get_filename_component( SOURCE_FILE_EXT ${source_file} EXT )
			if( ( SOURCE_FILE_EXT STREQUAL ".cpp" ) OR ( SOURCE_FILE_EXT STREQUAL ".cxx" ) OR ( SOURCE_FILE_EXT STREQUAL ".cc" ) )
				math( EXPR unitybuild_file_count "${unitybuild_file_count} + 1" )
				if( NOT( unitybuild_file_count LESS GOLAEM_USE_UNITY_BUILDS_FILESCOUNT ) )
					set( unitybuild_file_count "0" )
				endif( NOT( unitybuild_file_count LESS GOLAEM_USE_UNITY_BUILDS_FILESCOUNT ) )
				file( APPEND "${UNITY_BUILD_FILE_PREFIX}_${unitybuild_file_count}.cpp" "#include <${source_file}>\n" )
				set_source_files_properties( ${source_file} PROPERTIES HEADER_FILE_ONLY ON )
			elseif( SOURCE_FILE_EXT STREQUAL ".c" )
				if( NOT ${unity_build_c_files} )
					set( unity_build_c_files ON )
					file( WRITE "${UNITY_BUILD_FILE_PREFIX}.c" "// Unity build file generated by CMake\n" )
					set( local_UNITY_BUILD_FILES "${local_UNITY_BUILD_FILES}" "${UNITY_BUILD_FILE_PREFIX}.c" )
				endif()
				file( APPEND "${UNITY_BUILD_FILE_PREFIX}.c" "#include <${source_file}>\n" )
				set_source_files_properties(${source_file} PROPERTIES HEADER_FILE_ONLY ON )
			endif()
		endforeach()

		if( MSVC )
			SOURCE_GROUP( "Unity Build Files" FILES ${local_UNITY_BUILD_FILES} )
		endif()
		list( APPEND ${SOURCE_FILES} "${local_UNITY_BUILD_FILES}" )
	endif()
	
endmacro()


#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro GENERATE_UNITY_BUILD
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Generates a unit cpp files including all sources, to speed up compile times, look up Unity build.
#
# Input parameters :
# - TARGET_NAME = Name of the target
# - SOURCE_FILES = List the source files to include
#
# Output parameters :
# - UNITY_BUILD_FILES = Variable containing the resulting unity build files
#
# Example :
#   generate_unity_build( "${TARGET_CORE}" "${CORE_ALLFILES}" UNITY_BUILD_FILES )

macro( generate_unity_build TARGET_NAME SOURCE_FILES UNITY_BUILD_FILES )

	file( GLOB previous_UNITY_BUILD_FILES ${CMAKE_CURRENT_BINARY_DIR}/*UnityBuild*.c* )
	if( previous_UNITY_BUILD_FILES )
		file( REMOVE ${previous_UNITY_BUILD_FILES} )
	endif()

	set( UNITY_BUILD_FILE_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}UnityBuild" )
	unset( local_UNITY_BUILD_FILES )

	if( NOT GOLAEM_USE_UNITY_BUILDS_FILESCOUNT )
		set( GOLAEM_USE_UNITY_BUILDS_FILESCOUNT "8" )
	endif()
	
	set( unitybuild_file_count "0" )
	while( unitybuild_file_count LESS GOLAEM_USE_UNITY_BUILDS_FILESCOUNT )
		set( current_UNITY_BUILD_FILE "${UNITY_BUILD_FILE_PREFIX}_${unitybuild_file_count}.cpp" )
		list( APPEND local_UNITY_BUILD_FILES "${current_UNITY_BUILD_FILE}" )
		file( WRITE ${current_UNITY_BUILD_FILE} "// Unity build file generated by CMake\n" )
		math( EXPR unitybuild_file_count "${unitybuild_file_count} + 1" )
	endwhile()
	
	set( unity_build_c_files OFF )
	foreach( source_file ${SOURCE_FILES} )
		get_filename_component( SOURCE_FILE_EXT ${source_file} EXT )
		if( ( SOURCE_FILE_EXT STREQUAL ".cpp" ) OR ( SOURCE_FILE_EXT STREQUAL ".cxx" ) OR ( SOURCE_FILE_EXT STREQUAL ".cc" ) )
			math( EXPR unitybuild_file_count "${unitybuild_file_count} + 1" )
			if( NOT( unitybuild_file_count LESS GOLAEM_USE_UNITY_BUILDS_FILESCOUNT ) )
				set( unitybuild_file_count "0" )
			endif( NOT( unitybuild_file_count LESS GOLAEM_USE_UNITY_BUILDS_FILESCOUNT ) )
			file( APPEND "${UNITY_BUILD_FILE_PREFIX}_${unitybuild_file_count}.cpp" "#include <${source_file}>\n" )
			set_source_files_properties( ${source_file} PROPERTIES HEADER_FILE_ONLY ON )
		elseif( SOURCE_FILE_EXT STREQUAL ".c" )
			if( NOT ${unity_build_c_files} )
				set( unity_build_c_files ON )
				file( WRITE "${UNITY_BUILD_FILE_PREFIX}.c" "// Unity build file generated by CMake\n" )
				list( APPEND local_UNITY_BUILD_FILES "${UNITY_BUILD_FILE_PREFIX}.c" )
			endif()
			file( APPEND "${UNITY_BUILD_FILE_PREFIX}.c" "#include <${source_file}>\n" )
			set_source_files_properties(${source_file} PROPERTIES HEADER_FILE_ONLY ON )
		endif()
	endforeach()

	if( MSVC )
		SOURCE_GROUP( "Unity Build Files" FILES ${local_UNITY_BUILD_FILES} )
	endif()
	set( ${UNITY_BUILD_FILES} "${local_UNITY_BUILD_FILES}" )

endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro GET_DEBUG_LIBRARIES
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Extract debug libraries contained in a list of libraries (i.e. any library preceded by the "debug" keyword or not precessed by any keyword)
#
# Input parameters :
# - LIBS = List of libraries to parse
#
# Output parameters :
# - DEBUG_LIBS = List of debug libraries extracted from libraries list
#
# Optional parameters :
# - LIB_PREFIX = Prefix to append to every outputted library
#
# Example :
#   get_debug_libraries( "${PROJECT_LIBS}" PROJECT_DEBUG_LIBS )
#   If PROJECT_LIBS variable contains the following libraries : "lib1;debug;lib2_d;optimized;lib2;lib3"
#   The resulting PROJECT_DEBUG_LIBS will then contains : "lib1;lib2_d;lib3"

macro( get_debug_libraries LIBS DEBUG_LIBS )

	unset( DEBUG_LIBS )
	set( local_DEBUG_LIBS "" )
	set( SEARCH_LIB_DEBUG_FLAG OFF )
	set( SEARCH_LIB_OPTIMIZED_FLAG OFF )
	set( LIB_PREFIX "" )
	if( ${ARGC} GREATER 2 )
		set( LIB_PREFIX "${ARGV2}")
	endif()
	foreach( library ${LIBS} ) 
		if( SEARCH_LIB_OPTIMIZED_FLAG )
			set( SEARCH_LIB_OPTIMIZED_FLAG OFF )
			set( SEARCH_LIB_DEBUG_FLAG OFF )
		elseif( SEARCH_LIB_DEBUG_FLAG )
			set( local_DEBUG_LIBS "${local_DEBUG_LIBS}" "${LIB_PREFIX}${library}" )
			set( SEARCH_LIB_OPTIMIZED_FLAG OFF )
			set( SEARCH_LIB_DEBUG_FLAG OFF )
		else()
			if( library STREQUAL "optimized" )
				set( SEARCH_LIB_OPTIMIZED_FLAG ON )
				set( SEARCH_LIB_DEBUG_FLAG OFF )
			elseif( library STREQUAL "debug" )
				set( SEARCH_LIB_OPTIMIZED_FLAG OFF )
				set( SEARCH_LIB_DEBUG_FLAG ON )
			else()
				set( local_DEBUG_LIBS "${local_DEBUG_LIBS}" "${LIB_PREFIX}${library}" )
				set( SEARCH_LIB_OPTIMIZED_FLAG OFF )
				set( SEARCH_LIB_DEBUG_FLAG OFF )
			endif()
		endif()
	endforeach() 
	set( ${DEBUG_LIBS} "${local_DEBUG_LIBS}" )

endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro GET_OPTIMIZED_LIBRARIES
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Extract optimized libraries contained in a list of libraries (i.e. any library preceded by the "optimized" keyword or not precessed by any keyword)
#
# Input parameters :
# - LIBS = List of libraries to parse
#
# Output parameters :
# - OPTIMIZED_LIBS = List of optimized libraries extracted from libraries list
#
# Optional parameters :
# - LIB_PREFIX = Prefix to append to every outputted library
#
# Example :
#   get_optimized_libraries( "${PROJECT_LIBS}" PROJECT_RELEASE_LIBS )
#   If PROJECT_LIBS variable contains the following libraries : "lib1;debug;lib2_d;optimized;lib2;lib3"
#   The resulting PROJECT_RELEASE_LIBS will then contains : "lib1;lib2;lib3"

macro( get_optimized_libraries LIBS OPTIMIZED_LIBS )

	unset( local_OPTIMIZED_LIBS )
	set( local_OPTIMIZED_LIBS "" )
	set( SEARCH_LIB_DEBUG_FLAG OFF )
	set( SEARCH_LIB_OPTIMIZED_FLAG OFF )
	set( LIB_PREFIX "" )
	if( ${ARGC} GREATER 2 )
		set( LIB_PREFIX "${ARGV2}")
	endif()
	foreach( library ${LIBS} ) 
		if( SEARCH_LIB_OPTIMIZED_FLAG )
			set( local_OPTIMIZED_LIBS "${local_OPTIMIZED_LIBS}" "${LIB_PREFIX}${library}" )
			set( SEARCH_LIB_OPTIMIZED_FLAG OFF )
			set( SEARCH_LIB_DEBUG_FLAG OFF )
		elseif( SEARCH_LIB_DEBUG_FLAG )
			set( SEARCH_LIB_OPTIMIZED_FLAG OFF )
			set( SEARCH_LIB_DEBUG_FLAG OFF )
		else()
			if( library STREQUAL "optimized" )
				set( SEARCH_LIB_OPTIMIZED_FLAG ON )
				set( SEARCH_LIB_DEBUG_FLAG OFF )
			elseif( library STREQUAL "debug" )
				set( SEARCH_LIB_OPTIMIZED_FLAG OFF )
				set( SEARCH_LIB_DEBUG_FLAG ON )
			else()
				set( local_OPTIMIZED_LIBS "${local_OPTIMIZED_LIBS}" "${LIB_PREFIX}${library}" )
				set( SEARCH_LIB_OPTIMIZED_FLAG OFF )
				set( SEARCH_LIB_DEBUG_FLAG OFF )
			endif()
		endif()
	endforeach() 
	set( ${OPTIMIZED_LIBS} "${local_OPTIMIZED_LIBS}" )

endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro DEDUCE_LIBRARY_FILENAME
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Deduce the full name of a library (append postfix, add extension according to platform ...)
#
# Input parameters :
# - LIBRARY_NAME = Name of the library to deduce (without any prefix/postfix nor extension)
#
# Output parameters :
# - LIBRARY_FILENAME_DEDUCED = Variable containing the library full name
#
# Optional parameters :
# - LIBRARY_TYPE_STATIC = Flag to force deducing a name for static or dynamic GCC library, with ".a" (resp. ".so") extension (use either "STATIC" or "SHARED" keywords)
#
# Example :
#   deduce_library_filename( "glmCore" GLM_CORE_LIBNAME "SHARED" )

MACRO( deduce_library_filename LIBRARY_NAME LIBRARY_FILENAME_DEDUCED )

	unset( local_LIBRARY_FILENAME_DEDUCED )
	set( FULL_POSTFIX "${BUILD_COMPILER_VERSION}${BUILD_ARCHITECTURE_VERSION}" )
	if( MSVC )
		if( FULL_POSTFIX )
			set(local_LIBRARY_FILENAME_DEDUCED "${LIBRARY_NAME}_${FULL_POSTFIX}.lib" )
		else()
			set(local_LIBRARY_FILENAME_DEDUCED "${LIBRARY_NAME}.lib")
		endif()
	elseif( CMAKE_COMPILER_IS_GNUCC )
		if( FULL_POSTFIX )
			set( local_LIBRARY_FILENAME_DEDUCED "lib${LIBRARY_NAME}_${FULL_POSTFIX}" )
		else()
			set( local_LIBRARY_FILENAME_DEDUCED "lib${LIBRARY_NAME}" )
		endif()
		if( ${ARGC} GREATER 2 )
			if( "${ARGV2}" STREQUAL "STATIC" )
				set( LIBRARY_TYPE_STATIC ON )
			elseif( "${ARGV2}" STREQUAL "SHARED" )
				set( LIBRARY_TYPE_STATIC OFF )
			else()
				report_message( "ERROR" "Invalid '${ARGV2}' flag set when deducing filename for library '${LIBRARY_NAME}' (use either 'STATIC' or 'SHARED' keywords)" )
			endif()
		else()
			set( LIBRARY_TYPE_STATIC "${GOLAEM_USE_STATIC_LIBRARIES}" )
		endif()
		if( LIBRARY_TYPE_STATIC )
			set( local_LIBRARY_FILENAME_DEDUCED "${local_LIBRARY_FILENAME_DEDUCED}.a" )
		else()
			set( local_LIBRARY_FILENAME_DEDUCED "${local_LIBRARY_FILENAME_DEDUCED}.so" )
		endif()
	else()
		set(local_LIBRARY_FILENAME_DEDUCED "${LIBRARY_NAME}")
	endif()
	set( ${LIBRARY_FILENAME_DEDUCED} "${local_LIBRARY_FILENAME_DEDUCED}" )

ENDMACRO()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro GET_LINK_LIBRARY_NAMES
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Generate the appropriate post-fixed library name for link (according the available compilers and configurations).
#
# Input parameters :
# - TARGET_LIBS = Set of target libraries (without any postfix nor extension)
#
# Output parameters :
# - LINK_LIBRARY_NAMES = Variable containing the resulting link libraries names
#
# Examples :
#   get_link_library_names( "${TARGET_LIBS}" LINK_LIBRARY_NAMES )
#   get_link_library_names( "glmCore" LINK_LIBRARY_NAMES )
#   get_link_library_names( "glmCore;glmMotion;glmPath" LINK_LIBRARY_NAMES )
#
# Tips :
# - Do not forget " " around the targets libs variable, or CMake will misinterpret the MACRO call...

macro( get_link_library_names TARGET_LIBS LINK_LIBRARY_NAMES )

	unset( local_LINK_LIBRARY_NAMES )
	set( FULL_POSTFIX "${BUILD_COMPILER_VERSION}${BUILD_ARCHITECTURE_VERSION}" )
	if( MSVC )
		foreach( target_lib ${TARGET_LIBS} )
			if( FULL_POSTFIX )
				list( APPEND local_LINK_LIBRARY_NAMES debug ${target_lib}_${FULL_POSTFIX}_d optimized ${target_lib}_${FULL_POSTFIX} )
			else()
				list( APPEND local_LINK_LIBRARY_NAMES debug ${target_lib}_d optimized ${target_lib} )
			endif()
		endforeach()
	else()
		foreach( target_lib ${TARGET_LIBS} )
			if( FULL_POSTFIX )
				if( CMAKE_BUILD_TYPE MATCHES "Debug" )
					list( APPEND local_LINK_LIBRARY_NAMES ${target_lib}_${FULL_POSTFIX}_d )
				else()
					list( APPEND local_LINK_LIBRARY_NAMES ${target_lib}_${FULL_POSTFIX} )
				endif()
			else()
				list( APPEND local_LINK_LIBRARY_NAMES debug ${target_lib}_d optimized ${target_lib} )
			endif()
		endforeach()
	endif()
	set( ${LINK_LIBRARY_NAMES} "${local_LINK_LIBRARY_NAMES}" )

endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro SET_TARGET_LIBRARY_POSTFIX
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Set the appropriate postfix to a library target (corresponding to the MSVC version and the debug/release configuration used).
#
# Input parameters :
# - TARGET_NAME = Name of the target to append postfix
#
# Example :
#   set_target_library_postfix( ${TARGET_LIB} )

macro( set_target_library_postfix TARGET_NAME )

	set( FULL_POSTFIX "${BUILD_COMPILER_VERSION}${BUILD_ARCHITECTURE_VERSION}" )
	if( FULL_POSTFIX )
		set_target_properties( ${TARGET_NAME} PROPERTIES DEBUG_POSTFIX "_${FULL_POSTFIX}_d" RELWITHDEBINFO_POSTFIX "_${FULL_POSTFIX}_rd" RELEASE_POSTFIX "_${FULL_POSTFIX}" )
	else()
		set_target_properties( ${TARGET_NAME} PROPERTIES DEBUG_POSTFIX "_d" RELWITHDEBINFO_POSTFIX "_rd" )
	endif()

endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro SET_TARGET_EXECUTABLE_POSTFIX
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Set the appropriate postfix to an executable target (corresponding to the debug/release configuration used only).
#
# Input parameters :
# - TARGET_NAME = Name of the target to append postfix
#
# Example :
#   set_target_executable_postfix( ${TARGET_EXE} )

macro( set_target_executable_postfix TARGET_NAME )

	set_target_properties( ${TARGET_NAME} PROPERTIES DEBUG_POSTFIX "_d" RELWITHDEBINFO_POSTFIX "_rd" )

endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro SET_TARGET_LIBRARY_CONFIG_STATIC
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Configure static library according to target platform.
#
# Input parameters :
# - TARGET_NAME = Name of the target to configure
#
# Example :
#   set_target_library_config_static( ${TARGET_EXE} )

macro( set_target_library_config_static TARGET_NAME )

	if( MSVC )
		set_target_properties( ${TARGET_NAME} PROPERTIES STATIC_LIBRARY_FLAGS "/MACHINE:X64" )
	endif()

endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro SET_TARGET_LIBRARY_CONFIG_SHARED
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Configure shared library according to target platform.
#
# Input parameters :
# - TARGET_NAME = Name of the target to configure
#
# Example :
#   set_target_library_config_shared( ${TARGET_EXE} )

macro( set_target_library_config_shared TARGET_NAME )

	if( NOT MSVC )
		set_target_properties( ${TARGET_NAME} PROPERTIES INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib" )
	endif()

endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro SET_TARGET_EXECUTABLE_CONFIG
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Configure executable according to target platform.
#
# Input parameters :
# - TARGET_NAME = Name of the target to configure
#
# Example :
#   set_target_executable_config( ${TARGET_EXE} )

macro( set_target_executable_config TARGET_NAME )

	if( NOT MSVC )
		set_target_properties( ${TARGET_NAME} PROPERTIES INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib" )
	endif()

endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro LIST_FILES
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   List & group files located in a given directory.
#
# Input parameters :
# - FOLDER_NAME = Name of the folder containing files to list (relative to CMAKE_CURRENT_SOURCE_DIR)
# - FILE_EXTENSIONS = List of file extensions (for each extension, list files with the given extension and append it into LIST_FILES_${EXT} variable + append it to LIST_FILES variable)
#
# Example :
#   list_files( "version" "h;cpp;rc" )
#     -> Files are listed into LIST_FILES_H, LIST_FILES_CPP and LIST_FILES_RC variables according to their extension
#     -> All files are listed in LIST_FILES variable
#
# Optional parameters :
# - REFERENCE_DIRECTORY = Directory used as reference for listing (by default, set to CMAKE_CURRENT_SOURCE_DIR value)
#
# Tips :
# - Files list of each extension is appended to the LIST_FILES_${EXT} variable, and all files are listed in the LIST_FILES variable.

macro( list_files FOLDER_NAME FILE_EXTENSIONS )

	set( SKIP_INCLUDE_DIR_VAR OFF )
	set( FOR_INSTALL_ONLY_VAR OFF )
	if( ( ${ARGC} GREATER 2 ) AND ( NOT "${ARGV2}" STREQUAL "" ) )
		set( DIRECTORY_REFERENCE "${ARGV2}/${FOLDER_NAME}" )
		if( ${ARGC} GREATER 3 ) 
			if ("${ARGV3}" STREQUAL "SKIP_INCLUDE_DIR")
				set( SKIP_INCLUDE_DIR_VAR ON )
			endif()
			if( "${ARGV3}" STREQUAL "FOR_INSTALL_ONLY" )
				set( FOR_INSTALL_ONLY_VAR ON )
			endif()
		endif()
	else()
		set( DIRECTORY_REFERENCE "${CMAKE_CURRENT_SOURCE_DIR}/${FOLDER_NAME}" )
	endif()
	if( EXISTS ${DIRECTORY_REFERENCE} )
		if( NOT SKIP_INCLUDE_DIR_VAR AND NOT FOR_INSTALL_ONLY_VAR)
			include_directories( ${DIRECTORY_REFERENCE} )
		endif()
		if( "${FOLDER_NAME}" STREQUAL "." )
			set( DIRECTORY_FILTER "Source Files" )
			set( DIRECTORY_LABEL "ROOT" )
		else()
			set( DIRECTORY_FILTER "Source Files/${FOLDER_NAME}" )
			string( REGEX REPLACE "/\\." "" DIRECTORY_FILTER ${DIRECTORY_FILTER} )
			string( REGEX REPLACE "\\./" "" DIRECTORY_FILTER ${DIRECTORY_FILTER} )
			string( REGEX REPLACE "/" "\\\\\\\\" DIRECTORY_FILTER ${DIRECTORY_FILTER} )
			set( DIRECTORY_LABEL "${FOLDER_NAME}" )
			string( REGEX REPLACE "/\\." "" DIRECTORY_LABEL ${DIRECTORY_LABEL} )
			string( REGEX REPLACE "\\./" "" DIRECTORY_LABEL ${DIRECTORY_LABEL} )
			string( REGEX REPLACE "/" "_" DIRECTORY_LABEL ${DIRECTORY_LABEL} )
			string( TOUPPER "${DIRECTORY_LABEL}" DIRECTORY_LABEL )
		endif()
		foreach( file_ext ${FILE_EXTENSIONS} )
			string( TOUPPER "${file_ext}" _FILE_EXT )
			unset( LIST_FILES_${DIRECTORY_LABEL}_${_FILE_EXT} )
			file( GLOB LIST_FILES_${DIRECTORY_LABEL}_${_FILE_EXT} ${DIRECTORY_REFERENCE}/*.${file_ext} )
			list( APPEND LIST_FILES "${LIST_FILES_${DIRECTORY_LABEL}_${_FILE_EXT}}" )
			list( APPEND LIST_FILES_${_FILE_EXT} "${LIST_FILES_${DIRECTORY_LABEL}_${_FILE_EXT}}" )
			if( NOT FOR_INSTALL_ONLY_VAR )
				source_group( "${DIRECTORY_FILTER}" FILES ${LIST_FILES_${DIRECTORY_LABEL}_${_FILE_EXT}} )
			endif()
			# unset( LIST_FILES_${DIRECTORY_LABEL}_${_FILE_EXT} )
		endforeach()
	else()
		report_message( "ERROR" "Directory '${DIRECTORY_REFERENCE}' does not exist (requested for files listing by '${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt')" )
	endif()

endmacro()


#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro ADD_PROJECT_OPTION
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Add a build option to the current project.
#
# Input parameters :
# - OPTION_NAME : name of the build option
# - OPTION_DESCRIPTION : description of the build option
# - OPTION_DEFAULT_STATUS : default status of the build option (ON/OFF)
#
# Examples :
#   add_project_option( "BUILD_SOURCES" "Build sources (libraries and executables) : ON/OFF" "ON" )
#   add_project_option( "BUILD_UNITTESTS" "Build samples : ON/OFF" "OFF" )

macro( add_project_option OPTION_NAME OPTION_DESCRIPTION OPTION_DEFAULT_STATUS )

	option( "${OPTION_NAME}" "${OPTION_DESCRIPTION}" "${OPTION_DEFAULT_STATUS}" )
	list( APPEND PROJECT_OPTIONS "${OPTION_NAME}" )

endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro ADD_OPTION_DEPENDENCY
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Specify a dependency to an existing build option.
#
# Input parameters :
# - OPTION_NAME : build options names
# - DEPENDENCY_NAME : name of the dependency
#
# Examples :
#	add_option_dependency( "BUILD_SOURCES" "Golaem Core" "${GLM_CORE_FOUND}" )
#	add_option_dependency( "BUILD_UNITTESTS" "CppUnit" "${CPPUNIT_FOUND}" )
#
# Tips :
# - BE CAREFUL with the variables syntax : 
#   - output parameters must be specified without brackets or quotes : VARIABLE syntax (reference of the variable).
# - The variable set as parameter DEPENDENCY_FOUND must have already been evaluated, i.e. a "FindPackage" 
#   request must have been done before calling the "ADD_OPTION_DEPENDENCY" macro.

macro( add_option_dependency OPTION_NAME DEPENDENCY_NAME DEPENDENCY_FOUND )

	if( ( NOT ${DEPENDENCY_FOUND} ) OR ( "${DEPENDENCY_FOUND}" STREQUAL "" ) )
		if( MISSING_DEPENDENCIES_${OPTION_NAME} )
			set( MISSING_DEPENDENCIES_${OPTION_NAME} "${MISSING_DEPENDENCIES_${OPTION_NAME}}, '${DEPENDENCY_NAME}'" )
		else()
			set( MISSING_DEPENDENCIES_${OPTION_NAME} "'${DEPENDENCY_NAME}'" )
		endif()
	endif()

endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro ADD_OPTIONS_DEPENDENCY
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Specify a dependency to a set of existing build option.
#
# Input parameters :
# - OPTIONS_NAMES : set of build options names
# - DEPENDENCY_NAME : name of the dependency
#
# Optional parameters :
# - DEPENDENCY_FOUND : name of the variable that indicates if the dependency has been found
#
# Examples :
#	add_options_dependency( "BUILD_TESTS_UNIT;BUILD_TESTS_PERF;BUILD_TESTS_MEMORY" "CppUnit" )
#	add_options_dependency(  "COMPONENT_QTCORE" "Golaem Core" "GLM_SDK_CORE_FOUND" )
#
# Tips :
# - BE CAREFUL with the variables syntax : 
#   - output parameters must be specified without brackets or quotes : VARIABLE syntax (reference of the variable).
# - The dependency referenced by DEPENDENCY_NAME must already have been found, e.g. the "FindPackage" command must have already been called on this dependency
#   request must have been done before calling the "ADD_OPTION_DEPENDENCY" macro.

macro( add_options_dependency OPTIONS_NAMES DEPENDENCY_NAME )

	if( ${ARGC} GREATER 2 )
		set( DEPENDENCY_FOUND "${ARGV2}" )
	else()
		string( TOUPPER ${DEPENDENCY_NAME} DEPENDENCY_NAME_UP )
		set( DEPENDENCY_FOUND "${DEPENDENCY_NAME_UP}_FOUND" )
	endif()
	if( ( NOT ${${DEPENDENCY_FOUND}} ) OR ( "${${DEPENDENCY_FOUND}}" STREQUAL "" ) )
		foreach( option_name ${OPTIONS_NAMES} )
			if( ${option_name} )
				if( MISSING_DEPENDENCIES_${option_name} )
					set( MISSING_DEPENDENCIES_${option_name} "${MISSING_DEPENDENCIES_${option_name}}, '${DEPENDENCY_NAME}'" )
				else()
					set( MISSING_DEPENDENCIES_${option_name} "'${DEPENDENCY_NAME}'" )
				endif()
			endif()
		endforeach()
	endif()

endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro FIND_DEPENDENCY
#-----------------------------------------------------------------------------------------------------------------------------------------------------
# 
# Description :
#   Find a dependency, for instance called FbxSdk, by calling the 'FindFbxSdk.cmake' script (located in CMAKE_MODULE_PATH, GOLAEM_MODULE_PATH or any of the specified folders).
# 
# Input parameters :
# - DEPENDENCY_NAME : name of the dependency
# 
# Optional parameters :
# - ADDITIONAL_SCRIPT_FOLDERS : list of folders where to look for CMake find script (by default, start looking only in CMAKE_MODULE_PATH and GOLAEM_MODULE_PATH)
# 
# Examples :
#	find_dependency( "FbxSdk" )
# 

macro( find_dependency DEPENDENCY_NAME )

	string( TOUPPER ${DEPENDENCY_NAME} DEPENDENCY_NAME_UP )
	if( "${${DEPENDENCY_NAME_UP}_FOUND}" STREQUAL "" )
		set( SCRIPT_DIRS "${CMAKE_MODULE_PATH}" "${GOLAEM_MODULE_PATH}" )
		if( ${ARGC} GREATER 1 )
			list( APPEND SCRIPT_DIRS "${ARGV1}" )
		endif()
		list( APPEND SCRIPT_DIRS "${CMAKE_ROOT}/Modules" )
		list( LENGTH SCRIPT_DIRS REMAINING_SCRIPT_DIRS_COUNT )
		while( REMAINING_SCRIPT_DIRS_COUNT GREATER 0 )
			list( GET SCRIPT_DIRS "0" script_dir )
			set( script_file_path "${script_dir}/Find${DEPENDENCY_NAME}.cmake" )
			if( EXISTS "${script_file_path}" )
				include( "${script_file_path}" )
				set( REMAINING_SCRIPT_DIRS_COUNT "0" )
			else()
				list( REMOVE_ITEM SCRIPT_DIRS "${script_dir}" )
				list( LENGTH SCRIPT_DIRS REMAINING_SCRIPT_DIRS_COUNT )
			endif()
		endwhile()
	endif()

endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro CHECK_BUILD_OPTION
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Check that an option is buildable (i.e. this build option is set to ON and none of its dependencies is missing).
#
# Input parameters :
# - OPTION_NAME : name of the build option
#
# Output parameters :
# - OPTION_BUILDABLE : variable set to ON if the option can be built
#
# Examples :
#   check_build_option( "BUILD_SOURCES" BUILD_SOURCES_CAPABLE )
#   check_build_option( "BUILD_UNITTESTS" BUILD_UNITTESTS_CAPABLE )
#
# Tips :
# - BE CAREFUL with the variables syntax : 
#   - output parameters must be specified without brackets or quotes : VARIABLE syntax (reference of the variable).

macro( check_build_option OPTION_NAME OPTION_BUILDABLE )

	set( local_OPTION_BUILDABLE OFF )
	if( ${OPTION_NAME} )
		if( MISSING_DEPENDENCIES_${OPTION_NAME} )
			report_message( "ERROR" "Missing dependencies (${MISSING_DEPENDENCIES_${OPTION_NAME}}) to build requested option '${OPTION_NAME}'" )
		else()
			set(local_OPTION_BUILDABLE ON)
		endif()
	endif()
	set( ${OPTION_BUILDABLE} "${local_OPTION_BUILDABLE}" )

endmacro()



#-----------------------------------------------------------------------------------------------------------------------------------------------------
# Macro PRINT_REPORT
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   Print reported errors/warnings/status information report.
#
# Examples :
#   print_report()

macro( print_report )

	message( "--------------------------------------------------------------------------------------------------" )
	message( "" )
	set( REPORT_TYPES "ERROR" "WARNING" "INFO" )
	foreach( report_type ${REPORT_TYPES} )
		if( REPORT_${report_type} )
			list( LENGTH REPORT_${report_type} REPORT_${report_type}_SIZE )
			foreach( report_entry ${REPORT_${report_type}} )
				message( "   ${report_type}: ${report_entry}" )
			endforeach()
			unset( REPORT_${report_type} CACHE )
		else()
			set( REPORT_${report_type}_SIZE "0" )
		endif()
	endforeach()
	message( "" )
	message( "   > ${REPORT_ERROR_SIZE} error(s), ${REPORT_WARNING_SIZE} warning(s), ${REPORT_INFO_SIZE} information message(s)" )
	message( "" )
		message( "--------------------------------------------------------------------------------------------------" )
	if( REPORT_ERROR_SIZE GREATER 0 )
		message( SEND_ERROR "${REPORT_ERROR_SIZE} ERRORS FOUND, SEE REPORT ABOVE !!!" )
	endif()

endmacro()
