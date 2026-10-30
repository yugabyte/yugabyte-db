# !/usr/bin/env python3
"""
Tests for MEKO-1105: text files over 1 MiB used to fail, because each 1 MiB
slice of the S3 body was decoded on its own and the S3 response was held open
for the whole embedding run.
"""

import json
import os
import tempfile
from unittest.mock import Mock, patch

import pytest

from rag_pipeline.chunk import chunk, get_splitter_for_filetype
from rag_pipeline.partition_chunk_pipeline import (
    MAX_FILE_SIZE_BYTES,
    read_file_stream,
    stream_partition_and_chunk,
)

PIPELINE_ID = 1
SLICE_SIZE = 1024 * 1024

# Devanagari "ka", 3 bytes in UTF-8, so a stream of them never aligns with a
# 1 MiB boundary.
DEVANAGARI_KA = "क"


class FakeS3Body:
    """Stand-in for botocore's StreamingBody that records its reads."""

    def __init__(self, data: bytes):
        self._data = data
        self._pos = 0
        self.read_sizes = []

    def read(self, amt=None):
        self.read_sizes.append(amt)
        if amt is None:
            data = self._data[self._pos:]
        else:
            data = self._data[self._pos:self._pos + amt]
        self._pos += len(data)
        return data


def make_s3_object(mock_boto3, text, content_length=None):
    """Wire mock_boto3 to serve `text` from s3 and return the body."""
    data = text.encode("utf-8") if isinstance(text, str) else text
    body = FakeS3Body(data)
    mock_s3_client = Mock()
    mock_boto3.client.return_value = mock_s3_client
    mock_s3_client.get_object.return_value = {
        "Body": body,
        "ContentLength": len(data) if content_length is None
        else content_length,
    }
    return mock_s3_client, body


def devanagari_text(byte_length=1536 * 1024):
    """Text whose bytes straddle the 1 MiB read boundary mid-character."""
    text = DEVANAGARI_KA * (byte_length // 3)
    data = text.encode("utf-8")
    # Guard the premise: byte SLICE_SIZE must be a continuation byte, so the
    # character it belongs to spans the boundary.
    assert data[SLICE_SIZE] & 0xC0 == 0x80
    return text


def expected_chunks(file_location, text):
    """The paragraph-then-chunk contract, applied to the whole text."""
    splitter, args = get_splitter_for_filetype(file_location)
    out = []
    for paragraph in text.split("\n\n"):
        stripped = "\n".join(
            line.rstrip("\n") for line in paragraph.split("\n") if line.strip()
        ).strip()
        if stripped:
            out.extend(chunk(splitter, stripped, args))
    return out


class TestMultiByteBoundary:
    """A multi-byte character straddling a read boundary must not fail."""

    @patch("rag_pipeline.partition_chunk_pipeline.boto3")
    def test_read_file_stream_decodes_across_slice_boundary(self, mock_boto3):
        text = devanagari_text()
        make_s3_object(mock_boto3, text)

        chunks = list(read_file_stream("s3://test-bucket/hindi.txt"))

        assert "".join(chunks) == text

    @patch("rag_pipeline.partition_chunk_pipeline.boto3")
    def test_read_file_stream_rejects_truncated_trailing_character(
        self, mock_boto3
    ):
        # Last character cut short: the final flush must surface it rather
        # than drop the bytes. read_file_stream reports decode failures as
        # RuntimeError, as it did before the incremental decoder.
        truncated = (DEVANAGARI_KA * 10).encode("utf-8")[:-1]
        make_s3_object(mock_boto3, truncated)

        with pytest.raises(RuntimeError, match="utf-8"):
            list(read_file_stream("s3://test-bucket/truncated.txt"))

    @patch("rag_pipeline.partition_chunk_pipeline.boto3")
    def test_stream_partition_decodes_across_slice_boundary(self, mock_boto3):
        # Two paragraphs, each over 1 MiB of 3-byte characters.
        half = devanagari_text()
        text = f"{half}\n\n{half}"
        make_s3_object(mock_boto3, text)

        chunks = list(stream_partition_and_chunk(
            PIPELINE_ID, "s3://test-bucket/hindi.txt", {}
        ))

        assert chunks
        assert all(DEVANAGARI_KA in c for c in chunks)
        assert "".join(chunks).count(DEVANAGARI_KA) >= len(half) // 1000


class TestSingleRead:
    """The S3 response must be drained before chunking starts."""

    @patch("rag_pipeline.partition_chunk_pipeline.boto3")
    def test_body_is_read_once_before_the_first_chunk(self, mock_boto3):
        text = "\n\n".join(f"Paragraph {i}. " * 50 for i in range(20))
        mock_s3_client, body = make_s3_object(mock_boto3, text)

        generator = stream_partition_and_chunk(
            PIPELINE_ID, "s3://test-bucket/doc.txt", {}
        )
        next(generator)

        # One get_object, one unsized read() that drained the body.
        mock_s3_client.get_object.assert_called_once_with(
            Bucket="test-bucket", Key="doc.txt"
        )
        assert body.read_sizes == [None]
        reads_before_chunking = len(body.read_sizes)

        rest = list(generator)

        assert rest
        assert len(body.read_sizes) == reads_before_chunking


class TestSizeLimit:
    """Nothing over the api-server's upload cap should be chunked."""

    def test_cap_matches_the_upload_limit(self):
        assert MAX_FILE_SIZE_BYTES == 5 * 1024 * 1024

    @patch("rag_pipeline.partition_chunk_pipeline.boto3")
    def test_oversize_s3_object_raises_before_any_chunk(self, mock_boto3):
        make_s3_object(
            mock_boto3,
            "Small body, large ContentLength.\n",
            content_length=MAX_FILE_SIZE_BYTES + 1,
        )

        generator = stream_partition_and_chunk(
            PIPELINE_ID, "s3://test-bucket/huge.txt", {}
        )

        with pytest.raises(ValueError):
            next(generator)

    @patch("rag_pipeline.partition_chunk_pipeline.boto3")
    def test_object_at_the_cap_is_chunked(self, mock_boto3):
        make_s3_object(
            mock_boto3,
            "At the limit.\n",
            content_length=MAX_FILE_SIZE_BYTES,
        )

        chunks = list(stream_partition_and_chunk(
            PIPELINE_ID, "s3://test-bucket/at-limit.txt", {}
        ))

        assert chunks == ["At the limit."]


class TestOutputUnchanged:
    """Files that worked before must chunk to the same text, in order."""

    SAMPLES = {
        "doc.md": "# Title\n\nFirst para.\nSecond line.\n\n## Section\n\nMore text.\n",
        "doc.txt": "Alpha line one.\nAlpha line two.\n\nBeta paragraph.\n",
        "doc.json": json.dumps({"a": 1, "b": ["x", "y"]}, indent=2) + "\n",
        "doc.csv": "col_a,col_b\n1,2\n3,4\n\n5,6\n",
        "doc.xml": "<root>\n  <item>one</item>\n</root>\n\n<tail/>\n",
        "doc.html": "<h1>Title</h1>\n\n<p>Body text.</p>\n",
    }

    @pytest.mark.parametrize("name", sorted(SAMPLES))
    def test_local_chunks_match_the_contract(self, name):
        text = self.SAMPLES[name]
        suffix = os.path.splitext(name)[1]

        with tempfile.NamedTemporaryFile(
            mode="w", delete=False, suffix=suffix, encoding="utf-8"
        ) as f:
            f.write(text)
            temp_file = f.name

        try:
            chunks = list(
                stream_partition_and_chunk(PIPELINE_ID, temp_file, {})
            )
            assert chunks == expected_chunks(temp_file, text)
        finally:
            os.unlink(temp_file)

    @pytest.mark.parametrize("name", sorted(SAMPLES))
    @patch("rag_pipeline.partition_chunk_pipeline.boto3")
    def test_s3_chunks_match_local_chunks(self, mock_boto3, name):
        text = self.SAMPLES[name]
        suffix = os.path.splitext(name)[1]
        make_s3_object(mock_boto3, text)

        s3_chunks = list(stream_partition_and_chunk(
            PIPELINE_ID, f"s3://test-bucket/{name}", {}
        ))

        with tempfile.NamedTemporaryFile(
            mode="w", delete=False, suffix=suffix, encoding="utf-8"
        ) as f:
            f.write(text)
            temp_file = f.name

        try:
            local_chunks = list(
                stream_partition_and_chunk(PIPELINE_ID, temp_file, {})
            )
        finally:
            os.unlink(temp_file)

        assert s3_chunks == local_chunks


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
