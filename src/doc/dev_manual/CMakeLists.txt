file(GLOB doxy_body/* DOXY_SRC)

if(build_common)
  set(DOC_DEPENDS_LIBS goby_common)
endif()

if(build_acomms)
  set(DOC_DEPENDS_LIBS "${DOC_DEPENDS_LIBS};goby_acomms")
endif()

if(build_util)
  set(DOC_DEPENDS_LIBS "${DOC_DEPENDS_LIBS};goby_util")
endif()

if(build_moos)
  set(DOC_DEPENDS_LIBS "${DOC_DEPENDS_LIBS};goby_moos")
endif()

if(build_middleware)
  set(DOC_DEPENDS_LIBS "${DOC_DEPENDS_LIBS};goby_middleware")
endif()

if(build_pb)
  set(DOC_DEPENDS_LIBS "${DOC_DEPENDS_LIBS};goby_pb")
endif()

add_custom_command(OUTPUT ${goby_SHARE_DIR}/doc/goby-dev.pdf
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/doxy2pdf.sh
  ARGS ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${goby_SHARE_DIR}/doc/goby-dev.pdf
  DEPENDS ${DOC_DEPENDS_LIBS} ${DOXY_SRC}
)

add_custom_command(OUTPUT ${goby_SHARE_DIR}/doc/html/index.html
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/doxy2html.sh
  ARGS ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${DOC_DEPENDS_LIBS} goby-dev.doxy.in ${DOXY_SRC}
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/goby-dev.doxy.in ${CMAKE_CURRENT_BINARY_DIR}/goby-dev.doxy @ONLY)

option(build_doc_pdf "Build PDF Developers Documentation" OFF)

if(build_doc_pdf)
  add_custom_target(dev-pdf ALL DEPENDS ${goby_SHARE_DIR}/doc/goby-dev.pdf)
endif()

option(build_doc_html "Build HTML Developers Documentation" ON)

if(build_doc_html)
  add_custom_target(dev-html ALL DEPENDS ${goby_SHARE_DIR}/doc/html/index.html)
endif()

set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES ${goby_SHARE_DIR}/doc/html)
