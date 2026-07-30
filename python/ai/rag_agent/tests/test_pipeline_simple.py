# !/usr/bin/env python3
"""
Simplified test suite for pipeline.py functions.

This test file focuses on testing the core functionality of pipeline.py
without complex mocking dependencies.
"""

import pytest
import tempfile
import os
from unittest.mock import Mock, patch
from rag_pipeline.partition_chunk_pipeline import read_file_stream, stream_partition_and_chunk

PIPELINE_ID = 1


class TestReadFileStream:
    """Test cases for read_file_stream function."""

    def test_read_local_file_small(self):
        """Test reading a small local file."""
        test_content = "This is a test file.\nWith multiple lines.\nAnd some content."

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            chunks = list(read_file_stream(temp_file, chunk_size=10))
            assert len(chunks) > 0
            assert ''.join(chunks) == test_content
        finally:
            os.unlink(temp_file)

    def test_read_local_file_large(self):
        """Test reading a large local file with small chunk size."""
        test_content = "Line " + "x" * 1000 + "\n"
        large_content = test_content * 100

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(large_content)
            temp_file = f.name

        try:
            chunks = list(read_file_stream(temp_file, chunk_size=1024))
            assert len(chunks) > 0
            assert ''.join(chunks) == large_content
        finally:
            os.unlink(temp_file)

    def test_read_local_file_empty(self):
        """Test reading an empty local file."""
        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            temp_file = f.name

        try:
            chunks = list(read_file_stream(temp_file))
            assert len(chunks) == 0
        finally:
            os.unlink(temp_file)

    def test_read_local_file_nonexistent(self):
        """Test reading a non-existent local file."""
        with pytest.raises(FileNotFoundError):
            list(read_file_stream("/nonexistent/file.txt"))

    def test_read_file_stream_unicode_content(self):
        """Test reading file with unicode content."""
        unicode_content = (
            "Hello \u4e16\u754c! \U0001F30D\n"
            "With emojis \U0001F680 and special chars \u00F1\u00E1\u00E9\u00ED\u00F3\u00FA"
        )

        with tempfile.NamedTemporaryFile(
            mode='w', delete=False, suffix='.txt', encoding='utf-8'
        ) as f:
            f.write(unicode_content)
            temp_file = f.name

        try:
            chunks = list(read_file_stream(temp_file))
            assert ''.join(chunks) == unicode_content
        finally:
            os.unlink(temp_file)

    def test_read_file_stream_different_chunk_sizes(self):
        """Test read_file_stream with different chunk sizes."""
        test_content = "A" * 1000

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            chunks = list(read_file_stream(temp_file, chunk_size=2000))
            assert len(chunks) == 1
            assert chunks[0] == test_content

            chunks = list(read_file_stream(temp_file, chunk_size=100))
            assert len(chunks) == 10
            assert ''.join(chunks) == test_content
        finally:
            os.unlink(temp_file)


class TestStreamPartitionAndChunk:
    """Test cases for stream_partition_and_chunk function."""

    def test_stream_partition_simple_paragraphs(self):
        """Test streaming partition with simple paragraphs."""
        test_content = """First paragraph.
With multiple lines.

Second paragraph.
Also with multiple lines.

Third paragraph."""

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            chunks = list(stream_partition_and_chunk(PIPELINE_ID, temp_file, {}))
            assert len(chunks) == 3
            assert "First paragraph" in chunks[0]
            assert "Second paragraph" in chunks[1]
            assert "Third paragraph" in chunks[2]
        finally:
            os.unlink(temp_file)

    def test_stream_partition_empty_paragraphs(self):
        """Test streaming partition with empty paragraphs."""
        test_content = """First paragraph.

Second paragraph.


Third paragraph."""

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            chunks = list(stream_partition_and_chunk(PIPELINE_ID, temp_file, {}))
            assert len(chunks) == 3
        finally:
            os.unlink(temp_file)

    def test_stream_partition_single_paragraph(self):
        """Test streaming partition with single paragraph."""
        test_content = "Single paragraph without any empty lines."

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            chunks = list(stream_partition_and_chunk(PIPELINE_ID, temp_file, {}))
            assert len(chunks) == 1
            assert chunks[0] == test_content
        finally:
            os.unlink(temp_file)

    def test_stream_partition_large_file(self):
        """Test streaming partition with a large file."""
        paragraphs = []
        for i in range(100):
            paragraphs.append(f"Paragraph {i}.\nWith multiple lines.\nAnd content.")

        test_content = "\n\n".join(paragraphs)

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            chunks = list(stream_partition_and_chunk(PIPELINE_ID, temp_file, {}))
            assert len(chunks) == 100
        finally:
            os.unlink(temp_file)

    def test_stream_partition_very_long_line(self):
        """Test streaming partition with very long lines."""
        long_line = "A" * 10000
        test_content = f"{long_line}\n\nAnother paragraph."

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            chunks = list(stream_partition_and_chunk(PIPELINE_ID, temp_file, {}))
            assert len(chunks) > 2
        finally:
            os.unlink(temp_file)


class TestIntegration:
    """Integration tests combining multiple functions."""

    def test_full_pipeline_local_file(self):
        """Test full pipeline with local file."""
        test_content = """First paragraph.
With multiple lines.

Second paragraph.
Also with content.

Third paragraph."""

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            chunks = list(stream_partition_and_chunk(PIPELINE_ID, temp_file, {}))
            assert len(chunks) == 3
        finally:
            os.unlink(temp_file)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
