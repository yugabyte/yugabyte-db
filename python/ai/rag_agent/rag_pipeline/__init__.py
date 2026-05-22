"""
RAG pipeline module for document ingestion and embedding generation.

This module contains the core components for processing documents, chunking
text, and orchestrating the RAG (Retrieval-Augmented Generation) pipeline.

Submodules are exposed via PEP 562 module ``__getattr__`` so that
``import rag_pipeline`` (which is triggered transitively whenever any
submodule like ``rag_pipeline.chunk`` is imported) does **not** eagerly load
``rag_handler`` or the task processors. Those modules transitively depend on
``embeddings``, while ``embeddings.embed`` itself imports from
``pdf_processing`` which imports ``rag_pipeline.chunk``. Eager imports here
would close the cycle and produce ``ImportError: cannot import name ...
from partially initialized module 'embeddings'``.

Consumers can keep using the same public names (e.g.
``from rag_pipeline import RagPipelineHandler``); they just resolve lazily on
first attribute access, by which point all dependent packages have finished
initializing.
"""

from importlib import import_module
from typing import Any

_LAZY_EXPORTS = {
    "RagPipelineHandler": ("rag_pipeline.rag_handler", "RagPipelineHandler"),
    "read_file_stream": (
        "rag_pipeline.partition_chunk_pipeline", "read_file_stream",
    ),
    "stream_partition_and_chunk": (
        "rag_pipeline.partition_chunk_pipeline",
        "stream_partition_and_chunk",
    ),
    "chunk": ("rag_pipeline.chunk", "chunk"),
    "chunk_langchain_docs": (
        "rag_pipeline.chunk", "chunk_langchain_docs",
    ),
    "CreateSourceProcessorForAWS_S3": (
        "rag_pipeline.aws_source_processor",
        "CreateSourceProcessorForAWS_S3",
    ),
    "UserPromptEmbedder": (
        "rag_pipeline.user_prompt_generation_processor",
        "UserPromptEmbedder",
    ),
    "DocumentPreprocessor": (
        "rag_pipeline.document_preprocessor", "DocumentPreprocessor",
    ),
}

__all__ = list(_LAZY_EXPORTS.keys())


def __getattr__(name: str) -> Any:
    target = _LAZY_EXPORTS.get(name)
    if target is None:
        raise AttributeError(
            f"module 'rag_pipeline' has no attribute {name!r}"
        )
    module_name, attr_name = target
    module = import_module(module_name)
    value = getattr(module, attr_name)
    globals()[name] = value
    return value


def __dir__() -> list:
    return sorted(list(globals().keys()) + list(_LAZY_EXPORTS.keys()))
