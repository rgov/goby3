protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS test.proto)

add_executable(goby_test_middleware_regex test.cpp  ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_test_middleware_regex goby_middleware goby_common dccl)

add_test(goby_test_middleware_regex ${goby_BIN_DIR}/goby_test_middleware_regex)

