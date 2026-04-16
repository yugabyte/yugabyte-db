set(kw_library kiwi)
set(kw_src md5.c options.c)

add_library(kw_library_static STATIC ${kw_src})
set_target_properties(kw_library_static PROPERTIES OUTPUT_NAME ${kw_library})

# Build YugabyteDB's version of Postgres before attempting to build Odyssey.
add_dependencies(kw_library_static postgres)
