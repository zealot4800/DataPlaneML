
FIND_PACKAGE(yaml-cpp REQUIRED)
FIND_PACKAGE(gflags REQUIRED)

set(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf CONFIG QUIET)
if (NOT Protobuf_FOUND)
    # Ubuntu/Debian packages often ship the library without ProtobufConfig.cmake.
    find_package(Protobuf REQUIRED)
endif()
message(STATUS "Using protobuf ${Protobuf_VERSION}")
message(STATUS "Protobuf include path : ${Protobuf_INCLUDE_DIRS}")
message(STATUS "Protobuf libraries : ${Protobuf_LIBRARIES}")

set(_grpc_config_hints
        "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/grpc"
        "/usr/lib/x86_64-linux-gnu/cmake/grpc"
        "/usr/lib/aarch64-linux-gnu/cmake/grpc"
        "/usr/lib64/cmake/grpc"
        "/usr/lib/cmake/grpc")

foreach(_grpc_config_dir IN LISTS _grpc_config_hints)
    if (EXISTS "${_grpc_config_dir}/gRPCConfig.cmake")
        if (NOT DEFINED gRPC_DIR
                OR gRPC_DIR STREQUAL "gRPC_DIR-NOTFOUND"
                OR gRPC_DIR MATCHES "^/usr/local/")
            set(gRPC_DIR "${_grpc_config_dir}" CACHE PATH "Path to gRPC package config" FORCE)
        endif()
        break()
    endif()
endforeach()

find_package(gRPC CONFIG REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")


set(RP4_SOURCES ${RP4_SOURCES} physical_memory.h
        parser.h
        global.h
        global.cpp
        executor.h
        neuron_primitive.h neuron_primitive.cpp
        processor.h gateway.h
        pipeline.h
        physical_layer.h entry.h matcher_c.h defs.h
        ../api_pb/controller_to_dataplane.pb.cc
        ../api_pb/controller_to_dataplane.grpc.pb.cc
        ../controller/ctrl_init_dataplane.h
        ../controller/ctrl_mod_table.h) #matcher.h

add_library(rp4 ${RP4_SOURCES})
target_link_libraries(rp4
        gflags yaml-cpp gRPC::grpc++_reflection gRPC::grpc++
        protobuf::libprotobuf pcap glog)
