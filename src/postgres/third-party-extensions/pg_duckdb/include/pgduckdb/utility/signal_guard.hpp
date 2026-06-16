
#pragma once

#include <signal.h>

namespace pgduckdb {

class ThreadSignalBlockGuard {
public:
	ThreadSignalBlockGuard(const ThreadSignalBlockGuard &) = delete;
	ThreadSignalBlockGuard(ThreadSignalBlockGuard &&) = delete;
	ThreadSignalBlockGuard &operator=(const ThreadSignalBlockGuard &) = delete;
	ThreadSignalBlockGuard &operator=(ThreadSignalBlockGuard &&) = delete;

	explicit ThreadSignalBlockGuard();
	~ThreadSignalBlockGuard();
	void unblock();

private:
	bool _blocked;
	sigset_t _saved_set;
};

} // namespace pgduckdb
