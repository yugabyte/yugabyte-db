"""
Embeddings module for generating and managing document embeddings.

This module provides functionality to generate embeddings for various file types
(text, PDF, video) and perform similarity search on embedded content.
"""

from embeddings.embed import EmbeddingsGenerator
from embeddings.embedding_user_promt import UserPromptEmbedder

__all__ = [
    "EmbeddingsGenerator",
    "UserPromptEmbedder",
]
