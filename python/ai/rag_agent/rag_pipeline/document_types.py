"""
Document type (MIME type) constants shared across the RAG pipeline.

This module is the single source of truth for:
  - Which MIME types are supported by the ingestion pipeline.
  - How logical worker-type labels (e.g. "TEXT", "PDF") map to MIME types
    for worker specialization.

Worker specialization is driven by the WORKER_DOCUMENT_TYPE env var. The
routing policy is:

  - ``PDF``  -> GPU worker, processes only PDF files.
  - ``TEXT`` (also the default for any unset / unknown value) -> non-GPU
    worker, processes every other supported MIME type.

Both the S3 ingestion path (which tags each document with a MIME type and
rejects unsupported types) and the polling worker (which filters the work
queue by MIME type) must agree on these values, so they are defined in one
place and imported by both.

NOTE: ``SUPPORTED_DOCUMENT_TYPES`` and ``WORKER_TYPE_TO_MIME_TYPES`` are
hand-maintained as two separate lists. When adding a new MIME type keep
both in sync: add it to ``SUPPORTED_DOCUMENT_TYPES`` AND to the appropriate
worker bucket (``PDF`` if it needs a GPU, ``TEXT`` otherwise).
"""

import mimetypes
from typing import Dict, FrozenSet, List

# Some extensions (notably ``.md``) are not present in Python 3.11's default
# ``mimetypes.types_map`` and slim Docker base images don't ship a
# ``/etc/mime.types`` that fills the gap, so ``mimetypes.guess_type`` returns
# ``(None, None)`` and otherwise-supported files get rejected by the
# ingestion pipeline. Register the mappings the pipeline cares about here so
# the fix is applied process-wide as soon as this module is imported.
_CUSTOM_MIME_TYPES: Dict[str, str] = {
    ".md": "text/markdown",
    ".markdown": "text/markdown",
}
for _ext, _mime in _CUSTOM_MIME_TYPES.items():
    mimetypes.add_type(_mime, _ext)

# Authoritative set of MIME types the ingestion pipeline will accept.
SUPPORTED_DOCUMENT_TYPES: FrozenSet[str] = frozenset({
    "text/plain",
    "application/json",
    "application/pdf",
    "text/markdown",
    "text/csv",
    "text/xml",
    "text/html",
})

# Logical worker-type -> MIME types it should process.
# Lists are alphabetically sorted for deterministic ordering in logs and
# SQL parameter binding.
WORKER_TYPE_TO_MIME_TYPES: Dict[str, List[str]] = {
    "TEXT": [
        "application/json",
        "text/csv",
        "text/html",
        "text/markdown",
        "text/plain",
        "text/xml",
    ],
    "PDF": ["application/pdf"],
}

# Default worker type used when WORKER_DOCUMENT_TYPE env var is unset or
# set to an unrecognized value. The default is the non-GPU worker so that
# GPU workers must be explicitly opted into via WORKER_DOCUMENT_TYPE=PDF.
DEFAULT_WORKER_TYPE: str = "TEXT"
