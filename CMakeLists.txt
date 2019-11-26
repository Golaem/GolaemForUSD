cmake_minimum_required(VERSION 3.13)

############################################################
# BEGIN Project
############################################################

project(${TARGET_GOLAEMUSD})

# List Files
list_files( "src" "h;cpp" )
list_files( "src/procedural" "h;cpp;json" )
list_files( "src/utils" "h;cpp" )
list_files( "src/version" "h;cpp;rc")

# Create a library
add_library( ${PROJECT_NAME} SHARED ${LIST_FILES} )
message("PXR_usd_LIBRARY=${PXR_usd_LIBRARY}")
target_link_libraries( ${PROJECT_NAME} usd)

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
