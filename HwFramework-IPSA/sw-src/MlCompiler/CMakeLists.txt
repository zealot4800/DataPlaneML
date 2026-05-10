add_executable(ml_compiler main.cpp)
target_include_directories(ml_compiler PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../rP4-compiler)