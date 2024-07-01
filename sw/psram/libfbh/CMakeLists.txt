# Note we are using INTERFACE so that the library can be configured per-app
# with compile-time defines

add_library(libfbh INTERFACE)

target_sources(libfbh INTERFACE
	${CMAKE_CURRENT_LIST_DIR}/fb_mono.c
	${CMAKE_CURRENT_LIST_DIR}/fb_mono.h
	)

target_include_directories(libfbh INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
        )

target_link_libraries(libfbh INTERFACE
	pico_base_headers
	hardware_dma
	hardware_pio
	hardware_interp
	)

pico_generate_pio_header(libfbh ${CMAKE_CURRENT_LIST_DIR}/fb_mono.pio)
