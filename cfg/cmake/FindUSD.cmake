#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
#   Copyright (C) Golaem S.A.  All Rights Reserved.
#
#   dev@golaem.com
#
#-----------------------------------------------------------------------------------------------------------------------------------------------------
#
# Description :
#   This scripts is responsible for finding and configuring variables to use 'USD' packages (compatible with 'USD 19.x, 20.x' packages).
#
# Output :
# - USD_FOUND = Katana found on this system ?
# - USD_ROOTDIR = Katana root directory
# - USD_INCDIR = Katana headers directory
# - USD_LIBS = Katana libraries

if( ( "${USD_FOUND}" STREQUAL "" ) OR ( NOT USD_FOUND ) )

	set( USD_FOUND OFF )
	set( USD_REFFILE "pxrConfig.cmake" )
	set( USD_REQUESTEDDIR "${USD_ROOTDIR}" )
	unset( USD_ROOTDIR CACHE )
	find_path( USD_ROOTDIR "${USD_REFFILE}" "${USD_REQUESTEDDIR}" NO_DEFAULT_PATH )
	if( USD_ROOTDIR )
		include("${USD_ROOTDIR}/pxrConfig.cmake")
		if( PXR_LIBRARIES)
			set( USD_FOUND ON )
		endif()
		mark_as_advanced( USD_FOUND)
	else()
		set( USD_FOUND OFF )
		message( SEND_ERROR "USD directory is not correctly set" )
	endif()
	mark_as_advanced( USD_FOUND USD_INCDIR USD_LIBS )

endif()
