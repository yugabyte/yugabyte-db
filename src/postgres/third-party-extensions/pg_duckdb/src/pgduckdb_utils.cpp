#include "pgduckdb/pgduckdb_utils.hpp"

#include <iomanip>

namespace pgduckdb {

duckdb::unique_ptr<duckdb::QueryResult>
DuckDBQueryOrThrow(duckdb::ClientContext &context, const std::string &query) {
	auto res = context.Query(query, false);
	if (res->HasError()) {
		res->ThrowError();
	}
	return res;
}

duckdb::unique_ptr<duckdb::QueryResult>
DuckDBQueryOrThrow(duckdb::Connection &connection, const std::string &query) {
	return DuckDBQueryOrThrow(*connection.context, query);
}

duckdb::unique_ptr<duckdb::QueryResult>
DuckDBQueryOrThrow(const std::string &query) {
	auto connection = pgduckdb::DuckDBManager::GetConnection();
	return DuckDBQueryOrThrow(*connection, query);
}

void
AppendEscapedUri(std::ostringstream &oss, const char *str) {
	if (str == nullptr) {
		return;
	}

	while (*str) {
		char c = *str++;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
		    c == '.' || c == '~') {
			oss << c;
		} else {
			oss << '%' << std::setfill('0') << std::setw(2) << std::hex << std::uppercase
			    << static_cast<int>(static_cast<unsigned char>(c));
		}
	}
}

} // namespace pgduckdb
