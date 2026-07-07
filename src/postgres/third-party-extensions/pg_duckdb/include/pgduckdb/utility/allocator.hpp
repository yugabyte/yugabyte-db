#pragma once

namespace pgduckdb {

template <class T>
struct DuckDBMallocator {
	typedef T value_type;

	DuckDBMallocator() = default;

	template <class U>
	constexpr DuckDBMallocator(const DuckDBMallocator<U> &) noexcept {
	}

	[[nodiscard]] T *
	allocate(std::size_t n) {
		if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
			throw std::bad_array_new_length();
		}

		auto p = static_cast<T *>(duckdb_malloc(n * sizeof(T)));
		if (p == nullptr) {
			throw std::bad_alloc();
		}

		return p;
	}

	void
	deallocate(T *p, std::size_t) noexcept {
		duckdb_free(p);
	}
};

template <class T, class U>
bool
operator==(const DuckDBMallocator<T> &, const DuckDBMallocator<U> &) {
	return true;
}

template <class T, class U>
bool
operator!=(const DuckDBMallocator<T> &, const DuckDBMallocator<U> &) {
	return false;
}

} // namespace pgduckdb
