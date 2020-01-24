cmake_minimum_required(VERSION 3.13)

############################################################
# BEGIN Project
############################################################

project(${TARGET_GOLAEMUSD})

if( CMAKE_COMPILER_IS_GNUCC )
    # flags needed for some usd includes (that use hash_set)
    add_flag_definitions( "CXX_FLAGS" "-Wno-deprecated")
endif()

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

     
else()
    target_link_libraries( ${PROJECT_NAME} usd)
endif()

# cannot compile debug - windows or linux
set( CROWD_INSTALL_SKIP_DEBUG ON )

string(REGEX REPLACE "^glm" "USD_" CUSTOM_PROJECT_LABEL "${PROJECT_NAME}" )

crowd_label_project( ${PROJECT_NAME} )
set_target_properties( ${PROJECT_NAME} PROPERTIES PROJECT_LABEL ${CUSTOM_PROJECT_LABEL} )

crowd_load_dependencies( ${PROJECT_NAME} )
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
