cmake_minimum_required(VERSION 3.13)

option (GOLAEMUSD_STANDALONE_BUILD "Standalone build: ON/OFF" ON)

############################################################
# BEGIN Project
############################################################

# Stand Alone
if(GOLAEMUSD_STANDALONE_BUILD)
    ############################################################
	# BEGIN Declare various macros
	############################################################
	macro( list_files FOLDER_NAME FILE_EXTENSIONS )
        set( SKIP_INCLUDE_DIR OFF )
        if( ( ${ARGC} GREATER 2 ) AND ( NOT "${ARGV2}" STREQUAL "" ) )
            set( DIRECTORY_REFERENCE "${ARGV2}/${FOLDER_NAME}" )
            if( ( ${ARGC} GREATER 3 ) AND ( "${ARGV3}" STREQUAL "SKIP_INCLUDE_DIR" ) )
                set( SKIP_INCLUDE_DIR ON )
            endif()
        else()
            set( DIRECTORY_REFERENCE "${CMAKE_CURRENT_SOURCE_DIR}/${FOLDER_NAME}" )
        endif()
        if( EXISTS ${DIRECTORY_REFERENCE} )
            if( NOT SKIP_INCLUDE_DIR )
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
                source_group( "${DIRECTORY_FILTER}" FILES ${LIST_FILES_${DIRECTORY_LABEL}_${_FILE_EXT}} )
                # unset( LIST_FILES_${DIRECTORY_LABEL}_${_FILE_EXT} )
            endforeach()
        else()
        endif()
    endmacro()
    ############################################################
    # END Declare various macros
    ############################################################

    project(GolaemUSD)

    # Add dependencies
    if(MSVC)
        # set this for FindFbxSdk
        set(BUILD_COMPILER_VERSION "vc140")
        set(BUILD_ARCHITECTURE_VERSION "x64")
    endif()

    set( CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cfg/cmake")
    
    find_package(Python REQUIRED)
    find_package(FbxSdk REQUIRED)
    find_package(USD REQUIRED)
   
    # Golaem Devkit dependency
    set( GOLAEMDEVKIT_ROOTDIR "Golaem Devkit Dir" CACHE PATH "Golaem Devkit directory" )
    set( GOLAEMDEVKIT_INCDIR "${GOLAEMDEVKIT_ROOTDIR}/include")
    set( GOLAEMDEVKIT_LIBDIR "${GOLAEMDEVKIT_ROOTDIR}/lib")
    if(MSVC)
        list(APPEND GOLAEMDEVKIT_LIBS "${GOLAEMDEVKIT_LIBDIR}/glmCore_${BUILD_COMPILER_VERSION}${BUILD_ARCHITECTURE_VERSION}.lib")
        list(APPEND GOLAEMDEVKIT_LIBS "${GOLAEMDEVKIT_LIBDIR}/glmCrowdIO.lib")
    else()
        list(APPEND GOLAEMDEVKIT_LIBS "${GOLAEMDEVKIT_LIBDIR}/libglmCrowdIO.so")
    endif(MSVC)

    # List Files
    list_files( "src" "h;cpp" )
    list_files( "src/procedural" "h;cpp;json" )
    list_files( "src/utils" "h;cpp" )
    list_files( "src/version" "h;cpp;rc")

    # Include and Link Dependencies
    add_library( ${PROJECT_NAME} SHARED ${LIST_FILES} )
    target_include_directories(${PROJECT_NAME} PRIVATE ${PYTHON_INCDIR})
    target_include_directories(${PROJECT_NAME} PRIVATE ${FBXSDK_INCDIR})
    target_include_directories(${PROJECT_NAME} PRIVATE ${GOLAEMDEVKIT_INCDIR} )
    target_include_directories(${PROJECT_NAME} PRIVATE ${PXR_INCLUDE_DIRS})
    set_target_properties( ${PROJECT_NAME} PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD_DEBUG ON )
    target_link_libraries( ${PROJECT_NAME} ${PYTHON_LIBS})
    target_link_libraries( ${PROJECT_NAME} ${FBXSDK_LIBS})
    target_link_libraries(${PROJECT_NAME} ${GOLAEMDEVKIT_LIBS} )
    target_link_libraries( ${PROJECT_NAME} usd)

    # cannot compile debug - windows or linux 
    set( CROWD_INSTALL_SKIP_DEBUG ON )

    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE "-DNOMINMAX" "/MP" "/nologo" "/wd4251")
    else()
        # flags needed for some usd includes (that use hash_set)
        target_compile_options(${PROJECT_NAME} PRIVATE "-Wno-deprecated")
    endif()

    # project label
    string(REGEX REPLACE "^glm" "USD_" CUSTOM_PROJECT_LABEL "${PROJECT_NAME}" )
    set_target_properties( ${PROJECT_NAME} PROPERTIES PROJECT_LABEL ${CUSTOM_PROJECT_LABEL} )
 
    # Update plugInfo.json
    file( READ "${CMAKE_CURRENT_SOURCE_DIR}/src/procedural/plugInfo.json" PLUGININFO_PATH_CONTENTS )
    string( REPLACE "@GOLAEMUSD_LIBRARY_PATH@" "${PROJECT_NAME}_d${CMAKE_SHARED_LIBRARY_SUFFIX}" PLUGININFO_PATH_CONTENTS_Debug ${PLUGININFO_PATH_CONTENTS} )
    string( REPLACE "@GOLAEMUSD_LIBRARY_PATH@" "${PROJECT_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}" PLUGININFO_PATH_CONTENTS_Release ${PLUGININFO_PATH_CONTENTS} )

    # Install path
    set( GOLAEM_INSTALL_PATH "${CMAKE_INSTALL_PREFIX}/GolaemForUSD" )
    set( GOLAEM_INSTALL_PATH_Debug "${GOLAEM_INSTALL_PATH}DBG" )
    set( GOLAEM_INSTALL_PATH_Release "${GOLAEM_INSTALL_PATH}" )

    foreach( configuration "Debug" "Release" )
        install (TARGETS ${PROJECT_NAME} RUNTIME DESTINATION "${GOLAEM_INSTALL_PATH_${configuration}}/procedurals/usd" CONFIGURATIONS ${configuration})
        set( PLUGININFO_PATH_${configuration} "${CMAKE_BINARY_DIR}/${configuration}/plugInfo.json" )
        file( WRITE "${PLUGININFO_PATH_${configuration}}" "${PLUGININFO_PATH_CONTENTS_${configuration}}" )
        install( FILES "${PLUGININFO_PATH_${configuration}}" CONFIGURATIONS ${configuration} DESTINATION "${GOLAEM_INSTALL_PATH_${configuration}}/procedurals/usd" )      # install plugInfo file
    endforeach()

# Included in Golaem Solution
else()

    project(${TARGET_GOLAEMUSD})

    # List Files
    list_files( "src" "h;cpp" )
    list_files( "src/procedural" "h;cpp;json" )
    list_files( "src/utils" "h;cpp" )
    list_files( "src/version" "h;cpp;rc")

    # Create a library
    add_library( ${PROJECT_NAME} SHARED ${LIST_FILES} )

    set_target_properties( ${PROJECT_NAME} PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD_DEBUG ON )

    if(BUILD_USD_HOUDINI)
        # from Houdini toolkit/samples/USD/CMakeLists.txt
        if(MSVC)
            target_link_libraries( ${PROJECT_NAME}
                Houdini # Standard Houdini libraries
                ${_houdini_hfs_root}/custom/houdini/dsolib/libgusd.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_ar.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_arch.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_gf.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_js.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_kind.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_pcp.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_plug.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_sdf.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_tf.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_usd.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_usdGeom.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_usdRi.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_usdShade.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_usdUtils.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_vt.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/libpxr_work.lib
                ${_houdini_hfs_root}/custom/houdini/dsolib/hboost_python-mt.lib
                ${PYTHON_LIBRARY}
            )
        else()
            # Link against Houdini libraries (including USD)
            if ( DEFINED ENV{HOUDINI_HDK_LINK_GUSD} )
                target_link_libraries( ${PROJECT_NAME}
                    Houdini					# Standard Houdini libraries
                    ${_houdini_hfs_root}/dsolib/libgusd.so
                )
            else ()
                target_link_libraries( ${PROJECT_NAME}
                    Houdini			        # Standard Houdini libraries
                )
            endif ()
        endif()
    elseif(BUILD_USD_KATANA)
        target_link_libraries(${PROJECT_NAME} 
            arch
            gf
            pcp
            sdf
            tf
            usd
            vt
        )
    else()
        target_link_libraries( ${PROJECT_NAME} usd)
    endif()

    # cannot compile debug - windows or linux
    set( CROWD_INSTALL_SKIP_DEBUG ON )

    string(REGEX REPLACE "^glm" "USD_" CUSTOM_PROJECT_LABEL "${PROJECT_NAME}" )

    crowd_label_project( ${PROJECT_NAME} )
    set_target_properties( ${PROJECT_NAME} PROPERTIES PROJECT_LABEL ${CUSTOM_PROJECT_LABEL} )

    crowd_load_dependencies( ${PROJECT_NAME} )

    if( CMAKE_COMPILER_IS_GNUCC )
        # flags needed for some usd includes (that use hash_set)
        target_compile_options(${PROJECT_NAME} PRIVATE "-Wno-deprecated")
    endif()

    set_target_prefix( ${PROJECT_NAME} PLUGIN )
    set_target_postfix( ${PROJECT_NAME} PLUGIN )

    set(USD_PROC_INSTALL_DIR "procedurals/usd")

    # name the dll "GolaemUSD" instead of "glmGolaemUSD"
    CROWD_INSTALL_RUNTIME_LIBRARY( ${PROJECT_NAME} "${USD_PROC_INSTALL_DIR}" "GolaemUSD")

    crowd_install_dependencies()

    # Update plugInfo.json
    file( READ "${CMAKE_CURRENT_SOURCE_DIR}/src/procedural/plugInfo.json" PLUGININFO_PATH_CONTENTS )
    # get_target_property(_baseName ${PROJECT_NAME} LOCATION_<CONFIG>)
    string( REPLACE "@GOLAEMUSD_LIBRARY_PATH@" "GolaemUSD_d${CMAKE_SHARED_LIBRARY_SUFFIX}" PLUGININFO_PATH_CONTENTS_Debug ${PLUGININFO_PATH_CONTENTS} )
    string( REPLACE "@GOLAEMUSD_LIBRARY_PATH@" "GolaemUSD${CMAKE_SHARED_LIBRARY_SUFFIX}" PLUGININFO_PATH_CONTENTS_Release ${PLUGININFO_PATH_CONTENTS} )

    foreach( configuration "Debug" "Release" )
        set( PLUGININFO_PATH_${configuration} "${CMAKE_BINARY_DIR}/${configuration}/plugInfo.json" )
        file( WRITE "${PLUGININFO_PATH_${configuration}}" "${PLUGININFO_PATH_CONTENTS_${configuration}}" )
        install( FILES "${PLUGININFO_PATH_${configuration}}" CONFIGURATIONS ${configuration} DESTINATION "${CROWD_INSTALL_PATH_${configuration}}/${USD_PROC_INSTALL_DIR}" )      # install plugInfo file
    endforeach()
endif()
