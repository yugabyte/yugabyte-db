import os
import threading
import time


class Capabilities:
    """Which pg_dist_rag objects the connected database has.

    During a rolling upgrade the agent runs on 0.0.3 before the extension
    moves to 0.0.4, so it checks for each newer object instead of assuming
    it. A present object is cached for the life of the process: there is no
    downgrade script, so nothing removes it. An absent one is checked again
    after RECHECK_SECONDS, so running agents start using the 0.0.4 objects
    soon after ALTER EXTENSION, without a restart.
    """

    MAINTENANCE_RUNS = "maintenance_runs"
    QUEUE_DOCUMENT = "queue_document"

    RECHECK_SECONDS = int(os.getenv("CAPABILITY_RECHECK_SECONDS", "60"))

    _CHECKS = {
        MAINTENANCE_RUNS: "SELECT to_regclass('dist_rag.maintenance_runs') IS NOT NULL",
        QUEUE_DOCUMENT: "SELECT to_regprocedure('dist_rag._queue_document(uuid)') IS NOT NULL",
    }

    _lock = threading.Lock()
    _present = set()
    _absent_since = {}

    @classmethod
    def has(cls, cursor, name: str) -> bool:
        """True if the object exists, checking on cursor when the cache can't say."""
        with cls._lock:
            if name in cls._present:
                return True
            checked = cls._absent_since.get(name)
            if checked is not None and time.monotonic() - checked < cls.RECHECK_SECONDS:
                return False
        cursor.execute(cls._CHECKS[name])
        present = bool(cursor.fetchone()[0])
        with cls._lock:
            if present:
                cls._present.add(name)
                cls._absent_since.pop(name, None)
            else:
                cls._absent_since[name] = time.monotonic()
        return present

    @classmethod
    def forget(cls, name: str) -> None:
        """Drop what's cached for name, after the database said it doesn't exist."""
        with cls._lock:
            cls._present.discard(name)
            cls._absent_since.pop(name, None)

    @classmethod
    def reset(cls) -> None:
        with cls._lock:
            cls._present.clear()
            cls._absent_since.clear()
