import logging
import os
from typing import Any, Dict, Generator, Optional

from rag_pipeline.md_parser import parse_markdown, split_embed_text
from rag_pipeline.partition_chunk_pipeline import read_whole_file
from observability import meko_observe


MARKDOWN_MIME_TYPES = frozenset({"text/markdown", "text/x-markdown"})
_ENV_FLAG = "ENABLE_FINETUNING"
MAX_MARKDOWN_FILE_SIZE_BYTES = 5 * 1024 * 1024  # 5 MiB


def is_markdown_finetuning_enabled(file_type: Optional[str]) -> bool:
    if file_type not in MARKDOWN_MIME_TYPES:
        return False
    return os.getenv(_ENV_FLAG, "false").strip().lower() == "true"


@meko_observe(name="Markdown Whole-File Chunking", as_type="span")
def chunk_markdown_whole_file(
    file_location: str,
    chunk_args: Optional[Dict[str, Any]] = None,
) -> Generator[str, None, None]:

    full_text = read_whole_file(
        file_location, max_size_bytes=MAX_MARKDOWN_FILE_SIZE_BYTES
    )

    doc = parse_markdown(full_text, source_file=file_location)
    logging.info(
        f"Markdown fine-tuning chunker: file={file_location}, "
        f"length={len(full_text)} chars, "
        f"title={doc.title!r}, chunks={doc.total_chunks}"
    )

    for chunk in doc.chunks:
        yield from split_embed_text(chunk.to_embed_text(doc.title))