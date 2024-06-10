
macro(build_kiwi)
	set(KIWI_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/third_party/kiwi)
	if (${PROJECT_BINARY_DIR} STREQUAL ${PROJECT_SOURCE_DIR})
		add_custom_command(
			OUTPUT  ${PROJECT_BINARY_DIR}/third_party/kiwi/kiwi/libkiwi${CMAKE_STATIC_LIBRARY_SUFFIX}
			COMMAND ${CMAKE_COMMAND} ${PROJECT_BINARY_DIR}/third_party/kiwi
			        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
			        -DMACHINARIUM_INCLUDE_DIRS=${MACHINARIUM_INCLUDE_DIRS}
			        -DMACHINARIUM_LIBRARIES=${MACHINARIUM_LIBRARIES}
			COMMAND ${CMAKE_MAKE_PROGRAM} -C ${PROJECT_BINARY_DIR}/third_party/kiwi
			WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/third_party/kiwi
		)
	else()
		add_custom_command(
			OUTPUT  ${PROJECT_BINARY_DIR}/third_party/kiwi/kiwi/libkiwi${CMAKE_STATIC_LIBRARY_SUFFIX}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/third_party/kiwi
			COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/third_party/kiwi ${PROJECT_BINARY_DIR}/third_party/kiwi
			COMMAND cd ${PROJECT_BINARY_DIR}/third_party/kiwi && ${CMAKE_COMMAND}
			        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
			        -DMACHINARIUM_INCLUDE_DIRS=${MACHINARIUM_INCLUDE_DIRS}
			        -DMACHINARIUM_LIBRARIES=${MACHINARIUM_LIBRARIES} .
			COMMAND ${CMAKE_MAKE_PROGRAM} -C ${PROJECT_BINARY_DIR}/third_party/kiwi
		)
	endif()
	add_custom_target(libkiwi ALL
		DEPENDS ${PROJECT_BINARY_DIR}/third_party/kiwi/kiwi/libkiwi${CMAKE_STATIC_LIBRARY_SUFFIX}
	)
	message(STATUS "Use shipped libkiwi: ${PROJECT_SOURCE_DIR}/third_party/kiwi")
	set (KIWI_LIBRARIES "${PROJECT_BINARY_DIR}/third_party/kiwi/kiwi/libkiwi${CMAKE_STATIC_LIBRARY_SUFFIX}")
	set (KIWI_FOUND 1)
	add_dependencies(build_libs libkiwi)
endmacro(build_kiwi)
