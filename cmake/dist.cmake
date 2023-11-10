
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${APP_DESCRIPTION}")

set(CPACK_PACKAGE_VERSION_MAJOR "${APP_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${APP_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${APP_VERSION_PATCH}")
set(CPACK_PACKAGE_VERSION "${APP_VERSION}")

set(CPACK_PACKAGE_VENDOR "${APP_AUTHOR}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${APP_URL}")

set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")

if(UNIX AND NOT APPLE)
    include(dist_unix_bundle)
elseif(WIN32)
    include(dist_windows_iss)
endif()

include(CPack)
