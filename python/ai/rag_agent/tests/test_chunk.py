# !/usr/bin/env python3
"""
Test suite for chunk.py functions.

This test file focuses on testing the core functionality of chunk.py
including the chunk function and SPLITTERS dictionary.
"""

import pytest
import json
from unittest.mock import Mock, patch, MagicMock

from rag_pipeline.chunk import chunk, SPLITTERS


class TestChunk:
    """Test cases for chunk function."""

    def test_chunk_character_splitter(self):
        """Test chunk function with character splitter."""
        text = "This is a test document with multiple sentences. It should be split into chunks."
        args = '{"chunk_size": 20, "chunk_overlap": 5}'

        result = chunk("character", text, args)

        assert isinstance(result, list)
        assert len(result) > 0
        for chunk_text in result:
            assert isinstance(chunk_text, str)

    def test_chunk_recursive_character_splitter(self):
        """Test chunk function with recursive character splitter."""
        text = "This is a test document with multiple sentences. It should be split into chunks."
        args = '{"chunk_size": 50, "chunk_overlap": 10}'

        result = chunk("recursive_character", text, args)

        assert isinstance(result, list)
        assert len(result) > 0
        for chunk_text in result:
            assert isinstance(chunk_text, str)

    def test_chunk_markdown_splitter(self):
        """Test chunk function with markdown splitter."""
        text = """# Header 1
This is a paragraph.

## Header 2
Another paragraph with **bold** text.

### Header 3
Final paragraph."""
        args = '{"chunk_size": 100, "chunk_overlap": 20}'

        result = chunk("markdown", text, args)

        assert isinstance(result, list)
        assert len(result) > 0
        for chunk_text in result:
            assert isinstance(chunk_text, str)

    def test_chunk_python_splitter(self):
        """Test chunk function with python code splitter."""
        text = """def hello_world():
    print("Hello, World!")
    return "success"

class TestClass:
    def method(self):
        pass"""
        args = '{"chunk_size": 200, "chunk_overlap": 50}'

        result = chunk("python", text, args)

        assert isinstance(result, list)
        assert len(result) > 0
        for chunk_text in result:
            assert isinstance(chunk_text, str)

    def test_chunk_latex_splitter(self):
        """Test chunk function with latex splitter."""
        text = """\\documentclass{article}
\\begin{document}
\\title{Test Document}
\\author{Test Author}
\\maketitle

\\section{Introduction}
This is a test document.

\\subsection{Details}
More content here.
\\end{document}"""
        args = '{"chunk_size": 150, "chunk_overlap": 30}'

        result = chunk("latex", text, args)

        assert isinstance(result, list)
        assert len(result) > 0
        for chunk_text in result:
            assert isinstance(chunk_text, str)

    def test_chunk_nltk_splitter(self):
        """Test chunk function with NLTK splitter."""
        try:
            import nltk
        except ImportError:
            pytest.skip("NLTK is not installed")

        text = (
            "This is a test document with multiple sentences. "
            "It should be split into chunks. "
            "Each sentence should be handled properly."
        )
        args = '{"chunk_size": 100, "chunk_overlap": 20}'

        result = chunk("nltk", text, args)

        assert isinstance(result, list)
        assert len(result) > 0
        for chunk_text in result:
            assert isinstance(chunk_text, str)

    def test_chunk_spacy_splitter(self):
        """Test chunk function with spaCy splitter."""
        try:
            import spacy
        except ImportError:
            pytest.skip("spaCy is not installed")

        text = (
            "This is a test document with multiple sentences. "
            "It should be split into chunks. "
            "Each sentence should be handled properly."
        )
        args = '{"chunk_size": 100, "chunk_overlap": 20}'

        result = chunk("spacy", text, args)

        assert isinstance(result, list)
        assert len(result) > 0
        for chunk_text in result:
            assert isinstance(chunk_text, str)

    def test_chunk_unknown_splitter(self):
        """Test chunk function with unknown splitter raises ValueError."""
        text = "This is a test document."
        args = '{"chunk_size": 20, "chunk_overlap": 5}'

        with pytest.raises(ValueError, match="Unknown splitter: unknown_splitter"):
            chunk("unknown_splitter", text, args)

    def test_chunk_invalid_json_args(self):
        """Test chunk function with invalid JSON args."""
        text = "This is a test document."
        args = '{"chunk_size": 20, "chunk_overlap": 5'  # Missing closing brace

        with pytest.raises(json.JSONDecodeError):
            chunk("character", text, args)

    def test_chunk_empty_text(self):
        """Test chunk function with empty text."""
        text = ""
        args = '{"chunk_size": 20, "chunk_overlap": 5}'

        result = chunk("character", text, args)

        assert isinstance(result, list)
        assert len(result) == 0 or result == [""]

    def test_chunk_very_small_chunk_size(self):
        """Test chunk function with very small chunk size."""
        text = "This is a test document with multiple words."
        args = '{"chunk_size": 1, "chunk_overlap": 0}'

        result = chunk("character", text, args)

        assert isinstance(result, list)
        assert len(result) > 0
        assert len(result) == 1
        for chunk_text in result:
            assert isinstance(chunk_text, str)

    def test_chunk_large_chunk_size(self):
        """Test chunk function with large chunk size."""
        text = "This is a test document with multiple words."
        args = '{"chunk_size": 1000, "chunk_overlap": 100}'

        result = chunk("character", text, args)

        assert isinstance(result, list)
        assert len(result) > 0
        assert len(result) <= 2

    def test_chunk_zero_chunk_overlap(self):
        """Test chunk function with zero chunk overlap."""
        text = "This is a test document with multiple words that should be split."
        args = '{"chunk_size": 20, "chunk_overlap": 0}'

        result = chunk("character", text, args)

        assert isinstance(result, list)
        assert len(result) > 0
        for chunk_text in result:
            assert isinstance(chunk_text, str)

    def test_chunk_large_chunk_overlap(self):
        """Test chunk function with large chunk overlap."""
        text = "This is a test document with multiple words that should be split."
        args = '{"chunk_size": 20, "chunk_overlap": 15}'

        result = chunk("character", text, args)

        assert isinstance(result, list)
        assert len(result) > 0
        for chunk_text in result:
            assert isinstance(chunk_text, str)

    def test_chunk_whitespace_only_text(self):
        """Test chunk function with whitespace-only text."""
        text = "   \n\t   \n   "
        args = '{"chunk_size": 20, "chunk_overlap": 5}'

        result = chunk("character", text, args)

        assert isinstance(result, list)
        assert len(result) >= 0

    def test_chunk_special_characters(self):
        """Test chunk function with special characters."""
        text = "This is a test with special chars: !@#$%^&*()_+-=[]{}|;':\",./<>?"
        args = '{"chunk_size": 30, "chunk_overlap": 5}'

        result = chunk("character", text, args)

        assert isinstance(result, list)
        assert len(result) > 0
        for chunk_text in result:
            assert isinstance(chunk_text, str)

    def test_chunk_unicode_text(self):
        """Test chunk function with unicode text."""
        text = (
            "This is a test with unicode: \u4f60\u597d\u4e16\u754c \U0001F30D "
            "\u00e9mojis and acc\u00e9nts"
        )
        args = '{"chunk_size": 30, "chunk_overlap": 5}'

        result = chunk("character", text, args)

        assert isinstance(result, list)
        assert len(result) > 0
        for chunk_text in result:
            assert isinstance(chunk_text, str)

    @patch('rag_pipeline.chunk.SPLITTERS')
    def test_chunk_character_splitter_mocked(self, mock_splitters):
        """Test chunk function with mocked character splitter."""
        mock_splitter = Mock()
        mock_splitter.split_text.return_value = ["chunk1", "chunk2", "chunk3"]
        mock_splitter_class = Mock(return_value=mock_splitter)

        mock_splitters.__contains__ = Mock(return_value=True)
        mock_splitters.__getitem__ = Mock(return_value=mock_splitter_class)

        text = "This is a test document."
        args = '{"chunk_size": 20, "chunk_overlap": 5}'

        result = chunk("character", text, args)

        mock_splitters.__getitem__.assert_called_once_with("character")
        mock_splitter_class.assert_called_once_with(chunk_size=20, chunk_overlap=5)

        mock_splitter.split_text.assert_called_once_with(text)

        assert result == ["chunk1", "chunk2", "chunk3"]


class TestSplitters:
    """Test cases for SPLITTERS dictionary."""

    def test_splitters_dictionary_keys(self):
        """Test that SPLITTERS dictionary contains expected keys."""
        expected_keys = {
            "character", "latex", "markdown", "nltk",
            "python", "recursive_character", "spacy"
        }

        assert set(SPLITTERS.keys()) == expected_keys

    def test_splitters_dictionary_values(self):
        """Test that SPLITTERS dictionary contains expected classes."""
        from langchain_text_splitters import (
            CharacterTextSplitter,
            LatexTextSplitter,
            MarkdownTextSplitter,
            NLTKTextSplitter,
            PythonCodeTextSplitter,
            RecursiveCharacterTextSplitter,
            SpacyTextSplitter
        )

        assert SPLITTERS["character"] == CharacterTextSplitter
        assert SPLITTERS["latex"] == LatexTextSplitter
        assert SPLITTERS["markdown"] == MarkdownTextSplitter
        assert SPLITTERS["nltk"] == NLTKTextSplitter
        assert SPLITTERS["python"] == PythonCodeTextSplitter
        assert SPLITTERS["recursive_character"] == RecursiveCharacterTextSplitter
        assert SPLITTERS["spacy"] == SpacyTextSplitter

    def test_splitters_dictionary_immutable(self):
        """Test that SPLITTERS dictionary structure is preserved after operations."""
        original_keys = set(SPLITTERS.keys())
        original_values = list(SPLITTERS.values())

        test_key = "character"
        assert test_key in SPLITTERS
        assert SPLITTERS[test_key] is not None

        assert set(SPLITTERS.keys()) == original_keys
        assert list(SPLITTERS.values()) == original_values


if __name__ == "__main__":
    pytest.main([__file__])
