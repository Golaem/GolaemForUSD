#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
#   Copyright (C) Golaem S.A.  All Rights Reserved.
#
#   dev@golaem.com
#
#-----------------------------------------------------------------------------------------------------------------------------------------------------

# This file contains build definitions and compiler flags commonly used in Golaem projects.

set( LIST_BUILD_FLAGS 
	"C_FLAGS"                       # Compiler C flags (all configurations)
	"C_FLAGS_DEBUG"                 # Compiler C flags (debug configuration only)
	"C_FLAGS_RELEASE"               # Compiler C flags (release configuration only)
	"CXX_FLAGS"                     # Compiler CPP flags (all configurations)
	"CXX_FLAGS_DEBUG"               # Compiler CPP flags (debug configuration only)
	"CXX_FLAGS_RELEASE"             # Compiler CPP flags (release configuration only)
	"SHARED_LINKER_FLAGS"           # Linker flags to use when building shared libraries (all configurations)
	"SHARED_LINKER_FLAGS_DEBUG"     # Linker flags to use when building shared libraries (debug configuration only)
	"SHARED_LINKER_FLAGS_RELEASE"   # Linker flags to use when building shared libraries (release configuration only)
	"EXE_LINKER_FLAGS"              # Linker flags to use when building executables (all configurations)
	"EXE_LINKER_FLAGS_DEBUG"        # Linker flags to use when building executables (debug configuration only)
	"EXE_LINKER_FLAGS_RELEASE"      # Linker flags to use when building executables (release configuration only)
)
foreach( flag ${LIST_BUILD_FLAGS} )
	set( CMAKE_${flag}_EXISTS ON )
endforeach()

macro( add_flag_definitions FLAG_NAME FLAG_DEFINITIONS )
	if( CMAKE_${FLAG_NAME}_EXISTS )
		set( CMAKE_${FLAG_NAME} "${CMAKE_${FLAG_NAME}} ${FLAG_DEFINITIONS}" )
	else()
		report_message( "WARNING" "Trying to add definitions to unknown 'CMAKE_${FLAG_NAME}' build flags" )
	endif()
endmacro()

macro( remove_flag_definitions FLAG_NAME FLAG_DEFINITIONS )
	if( CMAKE_${FLAG_NAME}_EXISTS )
		string( REPLACE " ${FLAG_DEFINITIONS}" "" CMAKE_${FLAG_NAME} "${CMAKE_${FLAG_NAME}}" )
	else()
		report_message( "WARNING" "Trying to remove definitions from unknown 'CMAKE_${FLAG_NAME}' build flags" )
	endif()
endmacro()

macro( enable_flags PACKAGE_NAME )
	string( TOUPPER ${PACKAGE_NAME} _PACKAGE )
	if( NOT ${_PACKAGE}_FLAGS_ENABLED )
		set( ${_PACKAGE}_FLAGS_ENABLED ON )
		foreach( definition ${${_PACKAGE}_DEFINITIONS} )
			add_definitions( ${definition} )
		endforeach()
		foreach( flag ${LIST_BUILD_FLAGS} )
			if( ${_PACKAGE}_${flag} )
				add_flag_definitions( "${flag}" "${${_PACKAGE}_${flag}}" )
			endif()
		endforeach()
	endif()
endmacro()

macro( disable_flags PACKAGE_NAME )
	string( TOUPPER ${PACKAGE_NAME} _PACKAGE )
	if( ${_PACKAGE}_FLAGS_ENABLED )
		set( ${_PACKAGE}_FLAGS_ENABLED OFF )
		foreach( definition ${${_PACKAGE}_DEFINITIONS} )
			remove_definitions( ${definition} )
		endforeach()
		foreach( flag ${LIST_BUILD_FLAGS} )
			if( ${_PACKAGE}_${flag} )
				remove_flag_definitions( "${flag}" "${${_PACKAGE}_${flag}}" )
			endif()
		endforeach()
	endif()
endmacro()

if( MSVC )

	add_definitions( "-DWIN32 -W4 /wd4324 /wd4251 /bigobj -DNOMINMAX -GR /MP /WX " )
	add_flag_definitions( "C_FLAGS" "/nologo" )
	add_flag_definitions( "C_FLAGS_DEBUG" "-D_DEBUG" )
	add_flag_definitions( "C_FLAGS_RELEASE" "-Ox -Oi -EHsc -MD -Ot -Ob2 -Oy -GF /Zi" )
	add_flag_definitions( "CXX_FLAGS" "/nologo" )
	add_flag_definitions( "CXX_FLAGS_DEBUG" "-D_DEBUG" )
	add_flag_definitions( "CXX_FLAGS_RELEASE" "-Ox -Oi -EHsc -MD -Ot -Ob2 -Oy -GF /Zi" )
	add_flag_definitions( "SHARED_LINKER_FLAGS_DEBUG" "/DEBUG /INCREMENTAL:NO" )
	add_flag_definitions( "SHARED_LINKER_FLAGS_RELEASE" "/DEBUG /INCREMENTAL:NO" )
	add_flag_definitions( "EXE_LINKER_FLAGS_DEBUG" "/DEBUG /INCREMENTAL:NO" )
	add_flag_definitions( "EXE_LINKER_FLAGS_RELEASE" "/DEBUG /INCREMENTAL:NO /OPT:REF,ICF" )

elseif( CMAKE_COMPILER_IS_GNUCC )	

	# set(CMAKE_CXX_VISIBILITY_PRESET hidden) # do not uncomment this as it would cause linux crashes on load/unload !
	# more infos on what it does here: https://gcc.gnu.org/wiki/Visibility
	add_definitions( "-DUNIX -Wall -Wno-unused-function -Wno-strict-aliasing -Wno-uninitialized -Wno-attributes -Werror" )
	add_flag_definitions( "C_FLAGS" "-fPIC -g" )
	add_flag_definitions( "C_FLAGS_DEBUG" "-D_DEBUG" )
	add_flag_definitions( "C_FLAGS_RELEASE" "-O3 -msse2" )
	add_flag_definitions( "CXX_FLAGS" "-g -fPIC -Wno-reorder" )
	add_flag_definitions( "CXX_FLAGS_DEBUG" "-D_DEBUG" )
	add_flag_definitions( "CXX_FLAGS_RELEASE" "-O3 -msse2" )
	# add_flag_definitions( "SHARED_LINKER_FLAGS" "-rdynamic" )
	# add_flag_definitions( "EXE_LINKER_FLAGS" "-rdynamic" )
	# add_flag_definitions( "SHARED_LINKER_FLAGS" "-rdynamic -Wl,-z,defs" )	

elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")

	add_definitions( "-DUNIX -Wall -Werror" )
	#add_flag_definitions( "C_FLAGS" "-fPIC -g" )	
	add_flag_definitions( "C_FLAGS" "--analyze -fPIC -g" )
	add_flag_definitions( "C_FLAGS_DEBUG" "-D_DEBUG" )
	add_flag_definitions( "C_FLAGS_RELEASE" "-O3 -msse2" )
	#add_flag_definitions( "CXX_FLAGS" "-fPIC -g -Wno-reorder" )
	add_flag_definitions( "CXX_FLAGS" "--analyze -fPIC -g -Wno-reorder" )	
	add_flag_definitions( "CXX_FLAGS_DEBUG" "-D_DEBUG" )
	add_flag_definitions( "CXX_FLAGS_RELEASE" "-O3 -msse2" )
	# add_flag_definitions( "SHARED_LINKER_FLAGS" "-rdynamic" )

endif()
