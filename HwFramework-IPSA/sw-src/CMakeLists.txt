include_directories(${CMAKE_CURRENT_SOURCE_DIR})
message( STATUS "DIR : ${CMAKE_CURRENT_SOURCE_DIR}" )

add_subdirectory(dataplane)
add_subdirectory(api)
add_subdirectory(controller)
if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests)
	add_subdirectory(tests)
endif()
add_subdirectory(MlCompiler)

