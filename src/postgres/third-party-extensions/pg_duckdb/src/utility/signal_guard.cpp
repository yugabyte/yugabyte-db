#include "pgduckdb/utility/signal_guard.hpp"

#include <stdexcept>

namespace pgduckdb {

ThreadSignalBlockGuard::ThreadSignalBlockGuard() : _blocked(false), _saved_set() {
	sigset_t new_set;
	if (sigfillset(&new_set) < 0) {
		throw std::runtime_error("sigfillset() failed");
	}

	const int err = pthread_sigmask(SIG_BLOCK, &new_set, &_saved_set);
	if (err != 0) {
		throw std::runtime_error("pthread_sigmask(SIG_BLOCK) failed");
	}

	_blocked = true;
}

ThreadSignalBlockGuard::~ThreadSignalBlockGuard() {
	unblock();
}

void
ThreadSignalBlockGuard::unblock() {
	if (!_blocked) {
		return;
	}

	const int err = pthread_sigmask(SIG_SETMASK, &_saved_set, /*old_set*/ nullptr);
	if (err != 0) {
		throw std::runtime_error("pthread_sigmask(SIG_SETMASK) failed");
	}

	_blocked = false;
}

} // namespace pgduckdb
