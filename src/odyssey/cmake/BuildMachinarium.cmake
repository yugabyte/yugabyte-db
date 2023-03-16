
macro(build_machinarium)
	set(MACHINARIUM_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/third_party/machinarium/sources)
	if (${PROJECT_BINARY_DIR} STREQUAL ${PROJECT_SOURCE_DIR})
		add_custom_command(
			OUTPUT  ${PROJECT_BINARY_DIR}/third_party/machinarium/sources/libmachinarium${CMAKE_STATIC_LIBRARY_SUFFIX}
			COMMAND ${CMAKE_COMMAND} ${PROJECT_BINARY_DIR}/third_party/machinarium
			        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
			        -DUSE_BORINGSSL=${USE_BORINGSSL}
			        -DBORINGSSL_ROOT_DIR=${BORINGSSL_ROOT_DIR}
			        -DBORINGSSL_INCLUDE_DIR=${BORINGSSL_INCLUDE_DIR}
			        -DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}
			        -DOPENSSL_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}
			        -DOPENSSL_LIBRARIES="${OPENSSL_LIBRARIES}"
			        -DBUILD_COMPRESSION=${BUILD_COMPRESSION}
			COMMAND ${CMAKE_MAKE_PROGRAM} -C ${PROJECT_BINARY_DIR}/third_party/machinarium
			WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/third_party/machinarium
		)
	else()
		add_custom_command(
			OUTPUT  ${PROJECT_BINARY_DIR}/third_party/machinarium/sources/libmachinarium${CMAKE_STATIC_LIBRARY_SUFFIX}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/third_party/machinarium
			COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/third_party/machinarium ${PROJECT_BINARY_DIR}/third_party/machinarium
			COMMAND cd ${PROJECT_BINARY_DIR}/third_party/machinarium && ${CMAKE_COMMAND}
			        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
			        -DUSE_BORINGSSL=${USE_BORINGSSL}
			        -DBORINGSSL_ROOT_DIR=${BORINGSSL_ROOT_DIR}
			        -DBORINGSSL_INCLUDE_DIR=${BORINGSSL_INCLUDE_DIR}
			        -DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}
			        -DOPENSSL_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}
			        -DOPENSSL_LIBRARIES="${OPENSSL_LIBRARIES}"
			        -DBUILD_COMPRESSION=${BUILD_COMPRESSION} .
			COMMAND ${CMAKE_MAKE_PROGRAM} -C ${PROJECT_BINARY_DIR}/third_party/machinarium
		)
	endif()
	add_custom_target(libmachinarium ALL
		DEPENDS ${PROJECT_BINARY_DIR}/third_party/machinarium/sources/libmachinarium${CMAKE_STATIC_LIBRARY_SUFFIX}
	)
	message(STATUS "Use shipped libmachinarium: ${PROJECT_SOURCE_DIR}/third_party/machinarium")
	set (MACHINARIUM_LIBRARIES "${PROJECT_BINARY_DIR}/third_party/machinarium/sources/libmachinarium${CMAKE_STATIC_LIBRARY_SUFFIX}")
	set (MACHINARIUM_FOUND 1)
	add_dependencies(build_libs libmachinarium)
endmacro(build_machinarium)
