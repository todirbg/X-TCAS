# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END

# Copyright 2017 Saso Kiselkov. All rights reserved.

cmake_minimum_required(VERSION 3.9)

if(WIN32)
	set(PLAT_SHORT "win64")
	set(PLAT_LONG "win-64")
	set(PLAT_ONLY "win")
	set(PLUGIN_BIN_OUTDIR "win_x64")
elseif(APPLE)
	set(PLAT_SHORT "mac64")
	set(PLAT_LONG "mac-64")
	set(PLAT_ONLY "mac")
	set(PLUGIN_BIN_OUTDIR "mac_x64")
else()
	set(PLAT_SHORT "lin64")
	set(PLAT_LONG "linux-64")
	set(PLAT_ONLY "linux")
	set(PLUGIN_BIN_OUTDIR "lin_x64")
endif()

SET(SRC SL.c dbg_log.c pos.c xtcas.c snd_sys.c)
SET(HDR SL.h dbg_log.h pos.h xtcas.h snd_sys.h)

set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELEASE} -DDEBUG")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG")

execute_process(COMMAND git rev-parse --short HEAD OUTPUT_VARIABLE XTCAS_VER)
string(REGEX REPLACE "\n$" "" XTCAS_VER "${XTCAS_VER}")


if(${XTCAS_VER} STREQUAL "") 
	set(XTCAS_VER "LOCAL")
else()
	string(REGEX REPLACE "\n$" "" XTCAS_VER "${XTCAS_VER}")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DXTCAS_VER=0x${XTCAS_VER}")

if(NOT DEFINED XPSDK)
	set(XPSDK "${CMAKE_SOURCE_DIR}/libraries/SDK")
endif()

if(NOT DEFINED LIBACFUTILS)
	set(LIBACFUTILS "${CMAKE_SOURCE_DIR}/libraries/libacfutils-redist")
endif()

if(NOT DEFINED VSI_DRAW_MODE)
	set(VSI_DRAW_MODE "1")
endif()

if(NOT DEFINED GTS820_MODE)
	set(GTS820_MODE "0")
endif()

if(NOT DEFINED VSI_STYLE)
	set(VSI_STYLE "1")
endif()

if(NOT DEFINED ${OPENAL})
	set(OPENAL "${CMAKE_SOURCE_DIR}/libraries/openal-soft")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DVSI_DRAW_MODE=${VSI_DRAW_MODE}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DGTS820_MODE=${GTS820_MODE}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DVSI_STYLE=${VSI_STYLE}")


include_directories( 
    "${XPSDK}/CHeaders/XPLM"
    "${XPSDK}/CHeaders/Widgets"
    "${XPSDK}/CHeaders/Wrappers"
    "${LIBACFUTILS}/include"
	"${LIBACFUTILS}/include/cglm"
	"../libraries/acf_apis"
)

if(WIN32)
	include_directories(xtcas PUBLIC "${LIBACFUTILS}/mingw64/include" "${OPENAL}/${PLAT_SHORT}/include/AL")
else()
	include_directories(xtcas PUBLIC "${LIBACFUTILS}/${PLAT_SHORT}/include" "${OPENAL}/${PLAT_SHORT}/include/AL")
endif()

if("${TEST_STANDALONE_BUILD}" EQUAL "1")
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DTEST_STANDALONE_BUILD")
	LIST(APPEND SRC test.c)
	LIST(APPEND HDR test.h)
else()
	LIST(APPEND SRC generic_intf.c xplane.c xplane_test.c ff_a320_intf.c
	    vsi.c)
	LIST(APPEND HDR ../xtcas/generic_intf.h xplane.h xplane_test.h
	    ff_a320_intf.h vsi.h)
endif()

SET(ALL_SRC ${SRC} ${HDR})
LIST(SORT ALL_SRC)

if(APPLE)
	add_executable(xtcas ${ALL_SRC})
else()
	if("${TEST_STANDALONE_BUILD}" EQUAL "1")
		add_executable(xtcas ${ALL_SRC})
	else()
		add_library(xtcas SHARED ${ALL_SRC})
	endif()
endif()

#compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DXPLM200=1 -DXPLM210=1")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -W -Wall -Wextra -Werror -fvisibility=hidden -Wno-unused-local-typedefs\
#	-Wunused-result -Wno-missing-field-initializers")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} \
	-DCHECK_RESULT_USED=\"__attribute__ ((warn_unused_result))\"")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64")

if(APPLE)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DPCRE2_CODE_UNIT_WIDTH=8 -DLACF_GLEW_USE_NATIVE_TLS=0")
endif()


if(APPLE)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAPL=1 -DIBM=0 -DLIN=0")
elseif(WIN32)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAPL=0 -DIBM=1 -DLIN=0 -D_WIN32_WINNT=0x0600")
else()
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAPL=0 -DIBM=0 -DLIN=1")
endif()

#libraries



#linking
link_libraries(m)
if(WIN32)
	set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "${LIBACFUTILS}/mingw64/lib")	
else()
	set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "${LIBACFUTILS}/${PLAT_SHORT}/lib")	
endif()

set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} ${OPENAL}/${PLAT_SHORT}/lib)

link_libraries(m)
if(APPLE)
	set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} ${XPSDK}/Libraries/Mac)

	FIND_LIBRARY(XPLM_LIBRARY XPLM)
	FIND_LIBRARY(XPWIDGETS_LIBRARY XPWidgets)
	FIND_LIBRARY(GLEW_LIBRARY GLEWmx)
	FIND_LIBRARY(OPENGL_LIBRARY OpenGL)
	FIND_LIBRARY(OPENAL_LIBRARY OpenAL)
	FIND_LIBRARY(CURSES_LIBRARY ncurses)
	FIND_LIBRARY(LIBACFUTILS_LIBRARY acfutils)
	FIND_LIBRARY(OPUSFILE_LIBRARY opusfile)
	FIND_LIBRARY(OPUS_LIBRARY opus)
	FIND_LIBRARY(OGG_LIBRARY ogg)
	FIND_LIBRARY(FREETYPE_LIBRARY freetype)
	FIND_LIBRARY(PIXMAN_LIBRARY pixman-1)
	FIND_LIBRARY(CAIRO_LIBRARY cairo)
	FIND_LIBRARY(CRYPTO_LIBRARY crypto)
	FIND_LIBRARY(CURL_LIBRARY curl)
	FIND_LIBRARY(SSL_LIBRARY ssl)	
	FIND_LIBRARY(LZMA_LIBRARY lzma)
	FIND_LIBRARY(LIBPNG_LIBRARY png16)
	FIND_LIBRARY(ZLIB_LIBRARY z)
	FIND_LIBRARY(COREFOUNDATION_LIBRARY CoreFoundation)
	FIND_LIBRARY(SYSTEMCONFIGURATION_LIBRARY SystemConfiguration)
	target_link_libraries(xtcas
	    ${LIBACFUTILS_LIBRARY}
	    ${OPENAL_LIBRARY}
		${GLEW_LIBRARY}
	    ${OPENGL_LIBRARY}
	    ${XPLM_LIBRARY}
	    ${XPWIDGETS_LIBRARY}
	    ${OPUSFILE_LIBRARY}
	    ${OPUS_LIBRARY}
	    ${OGG_LIBRARY}
	    ${CAIRO_LIBRARY}
	    ${PIXMAN_LIBRARY}
	    ${FREETYPE_LIBRARY}
	    ${LIBPNG_LIBRARY}
	    ${LZMA_LIBRARY}
		${CURL_LIBRARY}
		${SSL_LIBRARY}
		${CRYPTO_LIBRARY}
	    ${ZLIB_LIBRARY}
		${COREFOUNDATION_LIBRARY}
		${SYSTEMCONFIGURATION_LIBRARY}
	    )
elseif(WIN32)
	find_package(OpenGL REQUIRED)
	set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} ${XPSDK}/Libraries/Win)

	FIND_LIBRARY(XPLM_LIBRARY XPLM_64)
	FIND_LIBRARY(LIBACFUTILS_LIBRARY acfutils)
	FIND_LIBRARY(OPENAL_LIBRARY OpenAL32)
	FIND_LIBRARY(OPUSFILE_LIBRARY opusfile)
	FIND_LIBRARY(OPUS_LIBRARY opus)
	FIND_LIBRARY(OGG_LIBRARY ogg)
	FIND_LIBRARY(FREETYPE_LIBRARY freetype)
	FIND_LIBRARY(PIXMAN_LIBRARY pixman-1)
	FIND_LIBRARY(CAIRO_LIBRARY cairo)
	FIND_LIBRARY(LIBPNG_LIBRARY png16)
	FIND_LIBRARY(CRYPTO_LIBRARY crypto)
	FIND_LIBRARY(CURL_LIBRARY curl)
	FIND_LIBRARY(SSL_LIBRARY ssl)	
	FIND_LIBRARY(ZLIB_LIBRARY z)
	FIND_LIBRARY(GLEW_LIBRARY glew32mx)
	FIND_LIBRARY(LZMA_LIBRARY lzma)
	FIND_LIBRARY(CURL_LIBRARY curl)
	target_link_libraries(xtcas
		${LIBACFUTILS_LIBRARY}
		${XPLM_LIBRARY}
		${OPENAL_LIBRARY}
		${OPUSFILE_LIBRARY}
		${OPUS_LIBRARY}
		${OGG_LIBRARY}
		${CAIRO_LIBRARY}
		${PIXMAN_LIBRARY}
		${FREETYPE_LIBRARY}
		${LIBPNG_LIBRARY}
		${ZLIB_LIBRARY}
		${GLEW_LIBRARY}
		${LZMA_LIBRARY}
		${CURL_LIBRARY}
		${SSL_LIBRARY}
		${CRYPTO_LIBRARY}
		${OPENGL_LIBRARY}
		-lpsapi
		-ldbghelp
		-lcrypt32 
		-lws2_32 
		-lwinmm
		-lstdc++
		)
else()
	find_package(OpenGL REQUIRED)

	FIND_LIBRARY(LIBACFUTILS_LIBRARY acfutils)
	FIND_LIBRARY(OPUSFILE_LIBRARY opusfile)
	FIND_LIBRARY(OPENAL_LIBRARY openal)
	FIND_LIBRARY(OPUS_LIBRARY opus)
	FIND_LIBRARY(OGG_LIBRARY ogg)
	FIND_LIBRARY(FREETYPE_LIBRARY freetype)
	FIND_LIBRARY(PIXMAN_LIBRARY pixman-1)
	FIND_LIBRARY(CAIRO_LIBRARY cairo)
	FIND_LIBRARY(LIBPNG_LIBRARY png16)
	FIND_LIBRARY(CRYPTO_LIBRARY crypto)
	FIND_LIBRARY(SSL_LIBRARY ssl)	
	FIND_LIBRARY(ZLIB_LIBRARY z)
	FIND_LIBRARY(GLEW_LIBRARY GLEWmx)
	FIND_LIBRARY(LZMA_LIBRARY lzma)
	FIND_LIBRARY(CURL_LIBRARY curl)
	FIND_LIBRARY(PTHREAD_LIBRARY pthread)
	FIND_LIBRARY(LIBC_NONSHARED c_nonshared)
	target_link_libraries(xtcas
		${LIBACFUTILS_LIBRARY}
		${OPENGL_LIBRARY}
		${OPENAL_LIBRARY}
		${OPUSFILE_LIBRARY}
		${OPUS_LIBRARY}
		${OGG_LIBRARY}
		${CAIRO_LIBRARY}
		${PIXMAN_LIBRARY}
		${FREETYPE_LIBRARY}
		${LIBPNG_LIBRARY}
		${ZLIB_LIBRARY}
		${GLEW_LIBRARY}
		${LZMA_LIBRARY}
		${CURL_LIBRARY}
		${SSL_LIBRARY}
		${CRYPTO_LIBRARY}
		${PTHREAD_LIBRARY}
		${LIBC_NONSHARED}
		)
endif()

if("${TEST_STANDALONE_BUILD}" EQUAL "1")
	target_link_libraries(xtcas
		${CURSES_LIBRARY} )
endif()


SET_TARGET_PROPERTIES(xtcas PROPERTIES LINK_FLAGS "-Wl,--add-stdcall-alias")

if("${TEST_STANDALONE_BUILD}" EQUAL "1")
	set(OUTPUT_FILENAME "xtcas")
else()
	if(APPLE)
		SET_TARGET_PROPERTIES(xtcas PROPERTIES LINK_FLAGS
			"${CMAKE_SHARED_LINKER_FLAGS} -fPIC -fvisibility=hidden \
			-bundle")
	else()
			SET_TARGET_PROPERTIES(xtcas PROPERTIES LINK_FLAGS
			"${CMAKE_SHARED_LINKER_FLAGS} -undefined_warning -fPIC -fvisibility=hidden -static")
		if(UNIX AND NOT APPLE)
			SET_TARGET_PROPERTIES(xtcas PROPERTIES LINK_FLAGS
			"${CMAKE_SHARED_LINKER_FLAGS} -rdynamic -nodefaultlibs")
		endif()
	endif()
endif()



set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES PREFIX "")
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES SUFFIX "")
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY
	"${CMAKE_SOURCE_DIR}/Deploy/${CMAKE_PROJECT_NAME}/${PLUGIN_BIN_OUTDIR}/$<0:>" )
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY
	"${CMAKE_SOURCE_DIR}/Deploy/${CMAKE_PROJECT_NAME}/${PLUGIN_BIN_OUTDIR}/$<0:>" )
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME "${CMAKE_PROJECT_NAME}.xpl")
