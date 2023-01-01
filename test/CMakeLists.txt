# hex generator
separate_arguments(GENERATE_HEX)
macro(generate_hex TARGET)
	if(NOT "${GENERATE_HEX}" STREQUAL "" AND NOT "${GENERATE_HEX}" STREQUAL "None")
		#message("*** Generate Hex for ${TARGET}: ${GENERATE_HEX}")
		add_custom_command(TARGET ${TARGET}
			POST_BUILD
			COMMAND ${GENERATE_HEX} ${TARGET} ${TARGET}.hex
		)
	endif()
endmacro()

if(SERIES)
	# generate UsbDeviceTest for every board
	foreach(BOARD ${coco-devboards_COMPONENT_NAMES})
		string(REGEX REPLACE ".*\\:" "SpiMasterTest-" NAME ${BOARD})
		add_executable(${NAME}
			SpiMasterTest.cpp
		)
		target_include_directories(${NAME}
			PRIVATE
				../
		)

		# find link.ld of board
		get_target_property(INCLUDES ${BOARD} INTERFACE_INCLUDE_DIRECTORIES)
		target_link_directories(${NAME}
			PRIVATE	
				${INCLUDES}
		)

		target_link_libraries(${NAME}
			coco::coco
			${BOARD}
			coco-loop::coco-loop
			${PROJECT_NAME}
		)

		# windows specific libraries
		if(WIN32)
			target_link_libraries(${NAME} wsock32 ws2_32 winmm)
		endif()

		# generate hex file for flashing the target
		generate_hex(${NAME})
	endforeach()
endif()
