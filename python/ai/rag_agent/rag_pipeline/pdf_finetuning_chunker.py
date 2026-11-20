"""PDF -> Markdown -> structure-aware chunking for the fine-tuning path.

When fine-tuning is enabled, PDF documents are first converted to Markdown
locally with Docling (:mod:`pdf_processing.docling_loader`) and then chunked
with the same heading-aware Markdown parser used for native ``.md`` files
(:func:`rag_pipeline.md_parser.parse_markdown`). This keeps PDF and Markdown
fine-tuning chunks structurally consistent.

Gated by the shared ``ENABLE_FINETUNING`` flag so the default ingestion path
(unstructured-based PDF partitioning) is untouched unless fine-tuning is on.
"""

from __future__ import annotations

import logging
import os
from typing import Any, Dict, Generator, Optional

from rag_pipeline.md_parser import split_embed_text, stream_markdown
from observability import meko_observe

PDF_MIME_TYPES = frozenset({"application/pdf"})
_ENV_FLAG = "ENABLE_FINETUNING"


def is_pdf_finetuning_enabled(file_type: Optional[str]) -> bool:
    """True when ``file_type`` is a PDF and fine-tuning is enabled via env."""
    if file_type not in PDF_MIME_TYPES:
        return False
    return os.getenv(_ENV_FLAG, "false").strip().lower() == "true"


@meko_observe(name="PDF File Chunking (Docling)", as_type="span")
def chunk_pdf_whole_file(
    file_location: str,
    chunk_args: Optional[Dict[str, Any]] = None,
) -> Generator[str, None, None]:
    """Convert a PDF to Markdown via Docling, then yield embed-ready chunks.

    Args:
        file_location: Local path or ``s3://`` URI of the PDF.
        chunk_args: Accepted for signature parity with the other fine-tuning
            chunkers; the Markdown parser is structure-driven and does not
            currently consume these.

    Yields:
        One embedding-ready text block per heading-scoped Markdown section.
    """
    # Imported lazily so non-PDF workers never pull in the heavy Docling stack.
    from pdf_processing.docling_loader import convert_pdf_to_markdown

    markdown = convert_pdf_to_markdown(file_location)
    markdown_length = len(markdown)

    doc, chunks = stream_markdown(markdown, source_file=file_location)
    del markdown

    # Logged before the walk as well as after it, so a document that fails
    # part way through still leaves a line; the summary below needs the chunk
    # count, which is only known at the end.
    logging.info(
        f"PDF fine-tuning chunker: file={file_location}, "
        f"markdown_length={markdown_length} chars, "
        f"title={doc.title!r}, starting"
    )

    chunk_count = 0
    for chunk in chunks:
        chunk_count += 1
        yield from split_embed_text(chunk.to_embed_text(doc.title))

    logging.info(
        f"PDF fine-tuning chunker: file={file_location}, "
        f"markdown_length={markdown_length} chars, "
        f"title={doc.title!r}, chunks={chunk_count}"
    )
