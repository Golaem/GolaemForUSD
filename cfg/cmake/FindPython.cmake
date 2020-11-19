#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
#   Copyright (C) Golaem S.A.  All Rights Reserved.
#
#   dev@golaem.com
#
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   This scripts is responsible for finding and configuring variables to use 'Python' package.
#
# Output :
# - PYTHON_FOUND = Python found on this system ?
# - PYTHON_ROOTDIR = Python root directory
# - PYTHON_INCDIR = Python headers directory
# - PYTHON_LIBS = Python libraries

if( ( "${PYTHON_FOUND}" STREQUAL "" ) OR ( NOT PYTHON_FOUND ) )

	set( PYTHON_FOUND OFF )
	set( PYTHON_REFFILE "include/python2.7/pyconfig.h" )
	set( PYTHON_REQUESTEDDIR "${PYTHON_ROOTDIR}" )
	unset( PYTHON_ROOTDIR CACHE )
	find_path( PYTHON_ROOTDIR "${PYTHON_REFFILE}" "${PYTHON_REQUESTEDDIR}" NO_DEFAULT_PATH )
	if( PYTHON_ROOTDIR )
		set( PYTHON_FOUND ON )
		set( PYTHON_INCDIR "${PYTHON_ROOTDIR}/include/python2.7/" )
		if( MSVC )
			set( PYTHON_LIBS "${PYTHON_ROOTDIR}/lib/python27.lib")
		else()
			set( PYTHON_LIBS "${PYTHON_ROOTDIR}/lib/python27.a")
		endif()
	else()
		set( PYTHON_FOUND OFF )
		message( SEND_ERROR "Python directory is not correctly set" )
	endif()
	mark_as_advanced( PYTHON_FOUND PYTHON_INCDIR PYTHON_LIBS )

endif()
