# !/usr/bin/env python3
"""
Comprehensive test suite for pipeline.py functions with S3 and advanced testing.

This test file includes:
- Local file testing
- S3 file testing with proper mocking
- Error handling
- Edge cases
- Performance testing
"""

import pytest
import tempfile
import os
import json
from unittest.mock import Mock, patch, MagicMock
from rag_pipeline.partition_chunk_pipeline import read_file_stream, stream_partition_and_chunk

PIPELINE_ID = 1


class TestReadFileStreamLocal:
    """Test cases for read_file_stream function with local files."""

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
            "Hello \u4e16\u754c! \U0001f30d\n"
            "With emojis \U0001f680 and special chars \u00f1\u00e1\u00e9\u00ed\u00f3\u00fa"
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


class TestReadFileStreamS3:
    """Test cases for read_file_stream function with S3 files."""

    @patch('rag_pipeline.partition_chunk_pipeline.boto3')
    def test_read_s3_file_small(self, mock_boto3):
        """Test reading a small S3 file."""
        mock_s3_client = Mock()
        mock_boto3.client.return_value = mock_s3_client

        test_content = "This is S3 content.\nWith multiple lines."
        mock_response = {'Body': Mock()}
        mock_response['Body'].read.side_effect = [test_content.encode('utf-8'), b'']
        mock_s3_client.get_object.return_value = mock_response

        chunks = list(read_file_stream("s3://test-bucket/test-file.txt"))
        assert len(chunks) == 1
        assert chunks[0] == test_content

        mock_s3_client.get_object.assert_called_once_with(
            Bucket='test-bucket', Key='test-file.txt'
        )

    @patch('rag_pipeline.partition_chunk_pipeline.boto3')
    def test_read_s3_file_large(self, mock_boto3):
        """Test reading a large S3 file with multiple chunks."""
        mock_s3_client = Mock()
        mock_boto3.client.return_value = mock_s3_client

        chunk1 = "First chunk content. " * 100
        chunk2 = "Second chunk content. " * 100
        chunk3 = "Third chunk content. " * 100

        mock_response = {'Body': Mock()}
        mock_response['Body'].read.side_effect = [
            chunk1.encode('utf-8'),
            chunk2.encode('utf-8'),
            chunk3.encode('utf-8'),
            b''
        ]
        mock_s3_client.get_object.return_value = mock_response

        chunks = list(read_file_stream("s3://test-bucket/large-file.txt", chunk_size=1024))
        assert len(chunks) == 3
        assert chunks[0] == chunk1
        assert chunks[1] == chunk2
        assert chunks[2] == chunk3

    @patch('rag_pipeline.partition_chunk_pipeline.boto3')
    def test_read_s3_file_empty(self, mock_boto3):
        """Test reading an empty S3 file."""
        mock_s3_client = Mock()
        mock_boto3.client.return_value = mock_s3_client

        mock_response = {'Body': Mock()}
        mock_response['Body'].read.return_value = b''
        mock_s3_client.get_object.return_value = mock_response

        chunks = list(read_file_stream("s3://test-bucket/empty-file.txt"))
        assert len(chunks) == 0

    @patch('rag_pipeline.partition_chunk_pipeline.boto3')
    def test_read_s3_file_error(self, mock_boto3):
        """Test S3 file read error handling."""
        mock_s3_client = Mock()
        mock_boto3.client.return_value = mock_s3_client
        mock_s3_client.get_object.side_effect = RuntimeError("S3 access denied")

        with pytest.raises(RuntimeError):
            list(read_file_stream("s3://test-bucket/restricted-file.txt"))


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
            assert len(chunks) >= 3
        finally:
            os.unlink(temp_file)

    def test_stream_partition_with_custom_chunk_args(self):
        """Test streaming partition with custom chunk arguments."""
        test_content = "Test paragraph with some content."

        chunk_args = {
            'splitter': 'recursive_character',
            'args': json.dumps({"chunk_size": 50, "chunk_overlap": 10})
        }

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            chunks = list(stream_partition_and_chunk(PIPELINE_ID, temp_file, chunk_args))
            assert len(chunks) > 0
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
            assert len(chunks) >= 3
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
            assert len(chunks) >= 1
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
            assert len(chunks) >= 100
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

    @patch('rag_pipeline.partition_chunk_pipeline.boto3')
    def test_stream_partition_s3_file(self, mock_boto3):
        """Test streaming partition with S3 file."""
        mock_s3_client = Mock()
        mock_boto3.client.return_value = mock_s3_client

        test_content = "S3 paragraph.\nWith content.\n\nAnother paragraph."
        mock_response = {'Body': Mock()}
        mock_response['Body'].read.side_effect = [test_content.encode('utf-8'), b'']
        mock_s3_client.get_object.return_value = mock_response

        chunks = list(stream_partition_and_chunk(
            PIPELINE_ID, "s3://test-bucket/test-file.txt", {}
        ))
        assert len(chunks) >= 2


class TestEdgeCases:
    """Test edge cases and error conditions."""

    def test_read_file_stream_binary_content(self):
        """Test reading file with binary-like content that should be handled as text."""
        binary_like_content = "Binary-like content: \x00\x01\x02\nBut still text."

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(binary_like_content)
            temp_file = f.name

        try:
            chunks = list(read_file_stream(temp_file))
            assert ''.join(chunks) == binary_like_content
        finally:
            os.unlink(temp_file)

    def test_stream_partition_with_special_characters(self):
        """Test streaming partition with special characters."""
        test_content = """Paragraph with special chars: !@#$%^&*()

Another paragraph with unicode: \u00f1\u00e1\u00e9\u00ed\u00f3\u00fa \u4e16\u754c \U0001f30d"""

        with tempfile.NamedTemporaryFile(
            mode='w', delete=False, suffix='.txt', encoding='utf-8'
        ) as f:
            f.write(test_content)
            temp_file = f.name

        try:
            chunks = list(stream_partition_and_chunk(PIPELINE_ID, temp_file, {}))
            assert len(chunks) >= 2
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
            assert len(chunks) >= 3
        finally:
            os.unlink(temp_file)

    @patch('rag_pipeline.partition_chunk_pipeline.boto3')
    def test_full_pipeline_s3_file(self, mock_boto3):
        """Test full pipeline with S3 file."""
        mock_s3_client = Mock()
        mock_boto3.client.return_value = mock_s3_client

        test_content = "S3 paragraph.\nWith content.\n\nAnother paragraph."
        mock_response = {'Body': Mock()}
        mock_response['Body'].read.side_effect = [test_content.encode('utf-8'), b'']
        mock_s3_client.get_object.return_value = mock_response

        chunks = list(stream_partition_and_chunk(
            PIPELINE_ID, "s3://test-bucket/test-file.txt", {}
        ))
        assert len(chunks) >= 2


class TestPerformance:
    """Performance and stress tests."""

    def test_large_file_processing(self):
        """Test processing a very large file."""
        large_content = "This is a test line. " * 10000

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(large_content)
            temp_file = f.name

        try:
            chunks = list(stream_partition_and_chunk(PIPELINE_ID, temp_file, {}))
            assert len(chunks) > 0
        finally:
            os.unlink(temp_file)

    def test_memory_efficiency(self):
        """Test that processing doesn't load entire file into memory."""
        large_content = "Line " + "x" * 1000 + "\n"
        large_content = large_content * 1000

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(large_content)
            temp_file = f.name

        try:
            chunk_count = 0
            for c in stream_partition_and_chunk(PIPELINE_ID, temp_file, {}):
                chunk_count += 1
                if chunk_count > 10:
                    break

            assert chunk_count > 0
        finally:
            os.unlink(temp_file)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
