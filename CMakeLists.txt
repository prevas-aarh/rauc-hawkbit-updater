cmake_minimum_required(VERSION 3.0.2)
project( rauc-hawkbit-updater )

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(WITH_SYSTEMD "Set to ON to create unit files and enable systemd startup"  OFF)
option(BUILD_DOC    "Build documentation" OFF)
option(QA_BUILD     "QA build (pedantic, with fatal errors)" OFF)

find_package(PkgConfig REQUIRED)

if (WITH_SYSTEMD)
	add_definitions( -DWITH_SYSTEMD )
	pkg_check_modules(SYSTEMD REQUIRED libsystemd)
endif (WITH_SYSTEMD)

include_directories(${SYSTEMD_INCLUDE_DIRS})
if (SYSTEMD_FOUND AND "${SYSTEMD_SERVICES_INSTALL_DIR}" STREQUAL "")
	execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE}
		--variable=systemdsystemunitdir systemd
		OUTPUT_VARIABLE SYSTEMD_SERVICES_INSTALL_DIR)
	string(REGEX REPLACE "[ \t\n]+" "" SYSTEMD_SERVICES_INSTALL_DIR
		"${SYSTEMD_SERVICES_INSTALL_DIR}")
endif ()

pkg_check_modules(CURL REQUIRED libcurl>=7.47.0)
include_directories(${CURL_INCLUDE_DIRS})

pkg_check_modules(GIO REQUIRED gio-2.0>=2.26.0)
include_directories(${GIO_INCLUDE_DIRS})

pkg_check_modules(GIOUNIX REQUIRED gio-unix-2.0>=2.26.0)
include_directories(${GIOUNIX_INCLUDE_DIRS})

pkg_check_modules(JSONGLIB REQUIRED json-glib-1.0)
include_directories(${JSONGLIB_INCLUDE_DIRS})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

if (BUILD_DOC)
	# check if Doxygen is installed
	find_package(Doxygen)
	if (DOXYGEN_FOUND)
	    # set input and output files
	    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
	    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

	    # request to configure the file
	    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
	    message("Doxygen build started")

	    # note the option ALL which allows to build the docs together with the application
	    add_custom_target( doc_doxygen ALL
	        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
	        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	        COMMENT "Generating documentation with Doxygen"
	        VERBATIM )
	else (DOXYGEN_FOUND)
	  	message("Doxygen need to be installed to generate the doxygen documentation")
	endif (DOXYGEN_FOUND)
endif (BUILD_DOC)

find_program(SPHINX_EXECUTABLE
             NAMES sphinx-build
             DOC "Path to sphinx-build executable")
if (SPHINX_EXECUTABLE)
	message("Sphinx is installed")
	set(SPHINX_IN ${CMAKE_CURRENT_SOURCE_DIR}/docs)
	set(SPHINX_OUT ${CMAKE_CURRENT_BINARY_DIR}/build-doc)
	add_custom_target( doc
	    COMMAND ${SPHINX_EXECUTABLE} -b html ${SPHINX_OPTIONS} ${SPHINX_IN} ${SPHINX_OUT}
	    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	    COMMENT "Generating user documentation with Sphinx"
	    VERBATIM )
else (SPHINX_EXECUTABLE)
	message("Sphinx needs to be installed to generate the user documentation")
endif (SPHINX_EXECUTABLE)

set(RAUC_HAWKBIT_SRCS
  src/rauc-hawkbit-updater.c
  src/rauc-installer.c
  src/config-file.c
  src/hawkbit-client.c
  src/json-helper.c
  src/log.c
)

# if systemd append sd-helper
if (WITH_SYSTEMD)
	set(RAUC_HAWKBIT_SRCS
		${RAUC_HAWKBIT_SRCS}
		src/sd-helper.c
	)
endif (WITH_SYSTEMD)

set(CODEGEN_PREFIX rauc-installer-gen)
set(CODEGEN_COMMAND
    gdbus-codegen
      --generate-c-code ${CODEGEN_PREFIX}
      --interface-prefix de.pengutronix.rauc.
		  --c-namespace R
		  ${CMAKE_CURRENT_SOURCE_DIR}/src/rauc-installer.xml
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/${CODEGEN_PREFIX}.c
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/include/${CODEGEN_PREFIX}.h
		COMMAND ${CODEGEN_COMMAND}
		COMMAND mv ${CODEGEN_PREFIX}.c ${CMAKE_CURRENT_SOURCE_DIR}/src/
		COMMAND mv ${CODEGEN_PREFIX}.h ${CMAKE_CURRENT_SOURCE_DIR}/include/
)

include(GetGitRevisionDescription)
git_describe(VERSION_GIT)
set(VERSION_GIT_FULL "${VERSION_GIT}")

add_definitions( -DPROJECT_VERSION="${VERSION_GIT}" )

set(RAUC_HAWKBIT_SRCS
		${RAUC_HAWKBIT_SRCS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/${CODEGEN_PREFIX}.c
)

add_executable( rauc-hawkbit-updater ${RAUC_HAWKBIT_SRCS} )
target_compile_options(rauc-hawkbit-updater PUBLIC -Wall -Wformat-nonliteral)
if (QA_BUILD)
  target_compile_options(rauc-hawkbit-updater PUBLIC -Wextra -Werror -pedantic -Wbad-function-cast -Wcast-align -Wdeclaration-after-statement -Wformat=2 -Wshadow -Wno-unused-parameter -Wno-missing-field-initializers)
endif (QA_BUILD)

target_link_libraries( rauc-hawkbit-updater LINK_PUBLIC ${GIO_LIBRARIES}
                       ${JSONGLIB_LIBRARIES} ${CURL_LIBRARIES} ${SYSTEMD_LIBRARIES})
install (TARGETS rauc-hawkbit-updater DESTINATION /usr/bin/)

if (WITH_SYSTEMD)
  install (FILES ${CMAKE_SOURCE_DIR}/script/rauc-hawkbit-updater.service
	DESTINATION ${SYSTEMD_SERVICES_INSTALL_DIR}/)
endif (WITH_SYSTEMD)
