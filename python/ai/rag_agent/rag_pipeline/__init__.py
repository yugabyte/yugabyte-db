"""
RAG pipeline module for document ingestion and embedding generation.

This module contains the core components for processing documents, chunking text,
and orchestrating the RAG (Retrieval-Augmented Generation) pipeline.
"""

from rag_pipeline.rag_handler import RagPipelineHandler
from rag_pipeline.partition_chunk_pipeline import (
    read_file_stream,
    stream_partition_and_chunk,
)
from rag_pipeline.chunk import chunk, chunk_langchain_docs
from rag_pipeline.aws_source_processor import CreateSourceProcessorForAWS_S3
from rag_pipeline.user_prompt_generation_processor import UserPromptEmbedder
from rag_pipeline.document_preprocessor import DocumentPreprocessor

__all__ = [
    "RagPipelineHandler",
    "read_file_stream",
    "stream_partition_and_chunk",
    "chunk",
    "chunk_langchain_docs",
    "CreateSourceProcessorForAWS_S3",
    "DocumentPreprocessor",
    "UserPromptEmbedder",
]
