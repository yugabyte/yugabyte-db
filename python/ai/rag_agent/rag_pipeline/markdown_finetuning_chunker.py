import logging
import os
from typing import Any, Dict, Generator, Optional

from rag_pipeline.md_parser import split_embed_text, stream_markdown
from rag_pipeline.partition_chunk_pipeline import (
    max_file_size_bytes,
    read_whole_file,
)
from observability import meko_observe


MARKDOWN_MIME_TYPES = frozenset({"text/markdown", "text/x-markdown"})
_ENV_FLAG = "ENABLE_FINETUNING"


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
        file_location,
        max_size_bytes=max_file_size_bytes(),
        knob="MAX_FILE_SIZE_BYTES",
    )
    text_length = len(full_text)

    doc, chunks = stream_markdown(full_text, source_file=file_location)
    # stream_markdown holds the body it needs; dropping this reference frees
    # the other copy of the file while the chunks are still being produced.
    del full_text

    chunk_count = 0
    for chunk in chunks:
        chunk_count += 1
        yield from split_embed_text(chunk.to_embed_text(doc.title))

    # Logged on the way out, not on the way in: the chunk count is only known
    # once the document has been walked.
    logging.info(
        f"Markdown fine-tuning chunker: file={file_location}, "
        f"length={text_length} chars, "
        f"title={doc.title!r}, chunks={chunk_count}"
    )
