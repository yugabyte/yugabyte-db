import sys
import os

_rag_agent_dir = os.path.dirname(__file__)

# Skip rag_agent test collection when its dependencies are not installed
# (e.g., during the top-level `pytest python/` Jenkins run which uses the
# YB build virtualenv, not the rag_agent venv).
collect_ignore_glob = []
try:
    import psycopg_pool  # noqa: F401 -- rag_agent-specific dependency
    if _rag_agent_dir not in sys.path:
        sys.path.insert(0, _rag_agent_dir)
except ImportError:
    collect_ignore_glob = ["tests/test_*.py"]
