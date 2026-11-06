cmake_minimum_required(VERSION 3.21)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(PROJECT_VERSION "1.0.0.0" CACHE STRING "Project version")

include(target_add_versioninfo)
include(generate_ipp_translation_units)

project(GothicCS
	DESCRIPTION "GothicCS - Counter-Strike 1.6 gameplay for Gothic 1"
	VERSION ${PROJECT_VERSION}
)

if (NOT PROJECT_VERSION_PATCH)
	set(PROJECT_VERSION_PATCH 0)
endif()

if (NOT PROJECT_VERSION_TWEAK)
	set(PROJECT_VERSION_TWEAK 0)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# By default template uses dynamically linked union-api target to avoid compatibility problems.
# If you want to produce a standalone plugin, set this option to OFF
set(GENERATE_UNION_API_DLL ON)

file(GLOB_RECURSE IPP_FILES CONFIGURE_DEPENDS
	"src/*.ipp"
)

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS
	"src/*.cpp"
)

generate_ipp_translation_units(
	INPUT ${IPP_FILES}
	OUTPUT IPP_TRANSLATION_UNITS
)

add_library(${CMAKE_PROJECT_NAME} SHARED)

target_sources(${CMAKE_PROJECT_NAME}
	PRIVATE
		${SRC}
		${IPP_TRANSLATION_UNITS}
)

target_precompile_headers(${CMAKE_PROJECT_NAME}
	PRIVATE
		<Union/Hook.h>
		<ZenGin/zGothicAPI.h>
)

target_include_directories(${CMAKE_PROJECT_NAME}
	INTERFACE
		"${CMAKE_CURRENT_SOURCE_DIR}/include"
	PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_include_directories(${CMAKE_PROJECT_NAME}
 	BEFORE PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/userapi"
)

target_add_versioninfo(${CMAKE_PROJECT_NAME}
	VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_TWEAK}"
	COMPANY_NAME "Gothic Community"
	PRODUCT_NAME "${CMAKE_PROJECT_NAME}"
)

add_subdirectory(dependencies)

