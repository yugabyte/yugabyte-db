#pragma once

#include <string>
#include <vector>

namespace pgduckdb {

/* constants for duckdb.extensions */
#define Natts_duckdb_extension           3
#define Anum_duckdb_extension_name       1
#define Anum_duckdb_extension_autoload   2
#define Anum_duckdb_extension_repository 3

struct DuckdbExtension {
	DuckdbExtension() : name(), repository(), autoload(false) {
	}

	std::string name;
	std::string repository;
	bool autoload;
};

extern std::vector<DuckdbExtension> ReadDuckdbExtensions();

namespace ddb {

extern std::string LoadExtensionQuery(const std::string &extension_name);
extern std::string InstallExtensionQuery(const std::string &extension_name, const std::string &repository);

} // namespace ddb
} // namespace pgduckdb
