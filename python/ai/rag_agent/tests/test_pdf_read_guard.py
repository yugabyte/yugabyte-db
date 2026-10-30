# !/usr/bin/env python3
"""
Tests for MEKO-1128: the PDF path read an S3 object with no size check at all.

``_resolve_local_pdf_path`` called ``obj["Body"].read()`` into one bytes
object, so a large PDF from a crawled source could exhaust the worker before
Docling saw it. It now checks the length S3 reports against the same cap the
text and Markdown reads use, and copies the body in blocks.
"""

import io
import os
from unittest.mock import Mock, patch

import pytest

from pdf_processing.docling_loader import _resolve_local_pdf_path
from rag_pipeline.partition_chunk_pipeline import DEFAULT_MAX_FILE_SIZE_BYTES


class CountingBody(io.BytesIO):
    """An S3 body that records how it was read."""

    def __init__(self, data):
        super().__init__(data)
        self.read_calls = []

    def read(self, size=-1):
        self.read_calls.append(size)
        return super().read(size)


def make_s3_pdf(mock_boto3, data, content_length=None):
    body = CountingBody(data)
    client = Mock()
    mock_boto3.client.return_value = client
    client.get_object.return_value = {
        "Body": body,
        "ContentLength": len(data) if content_length is None
        else content_length,
    }
    return client, body


class TestPdfReadGuard:

    @patch("pdf_processing.docling_loader.boto3")
    def test_over_the_cap_raises_before_the_body_is_read(self, mock_boto3):
        _, body = make_s3_pdf(
            mock_boto3, b"%PDF-1.7 small",
            content_length=DEFAULT_MAX_FILE_SIZE_BYTES + 1,
        )

        with pytest.raises(ValueError, match="exceeds"):
            _resolve_local_pdf_path("s3://bucket/big.pdf")

        # The point of checking ContentLength first: the oversized body is
        # never pulled into the worker.
        assert body.read_calls == []

    @patch("pdf_processing.docling_loader.boto3")
    def test_the_error_names_the_file_and_the_limit(self, mock_boto3):
        make_s3_pdf(mock_boto3, b"x",
                    content_length=DEFAULT_MAX_FILE_SIZE_BYTES + 1)

        with pytest.raises(ValueError) as excinfo:
            _resolve_local_pdf_path("s3://bucket/big.pdf")

        message = str(excinfo.value)
        assert "s3://bucket/big.pdf" in message
        assert str(DEFAULT_MAX_FILE_SIZE_BYTES) in message
        # Names the setting that rejected it, as the text path does.
        assert "Raise MAX_FILE_SIZE_BYTES" in message

    @patch("pdf_processing.docling_loader.boto3")
    def test_a_pdf_at_the_cap_is_downloaded(self, mock_boto3):
        data = b"%PDF-1.7 " + b"a" * 100
        make_s3_pdf(mock_boto3, data,
                    content_length=DEFAULT_MAX_FILE_SIZE_BYTES)

        path, cleanup = _resolve_local_pdf_path("s3://bucket/ok.pdf")
        try:
            with open(path, "rb") as fh:
                assert fh.read() == data
        finally:
            cleanup()
        assert not os.path.exists(path)

    @patch("pdf_processing.docling_loader.boto3")
    def test_the_body_is_copied_in_blocks(self, mock_boto3):
        data = b"a" * (3 * 1024 * 1024)
        _, body = make_s3_pdf(mock_boto3, data)

        path, cleanup = _resolve_local_pdf_path("s3://bucket/ok.pdf")
        try:
            assert os.path.getsize(path) == len(data)
        finally:
            cleanup()

        # Fixed-size reads, so peak memory does not track the PDF's size.
        assert body.read_calls, "the body should have been read"
        assert all(size == 1024 * 1024 for size in body.read_calls), (
            f"expected 1 MiB blocks, got {body.read_calls}"
        )

    @patch("pdf_processing.docling_loader.boto3")
    def test_the_cap_override_applies(self, mock_boto3, monkeypatch):
        monkeypatch.setenv("MAX_FILE_SIZE_BYTES", "1024")
        make_s3_pdf(mock_boto3, b"x" * 2048)

        with pytest.raises(ValueError, match="1024-byte"):
            _resolve_local_pdf_path("s3://bucket/big.pdf")

    @patch("pdf_processing.docling_loader.boto3")
    def test_a_local_path_is_untouched(self, mock_boto3):
        path, cleanup = _resolve_local_pdf_path("/tmp/local.pdf")

        assert path == "/tmp/local.pdf"
        cleanup()
        mock_boto3.client.assert_not_called()

    @patch("pdf_processing.docling_loader.boto3")
    def test_a_missing_content_length_does_not_block_the_read(
        self, mock_boto3
    ):
        # enforce_size_limit skips a None size. A store that omits
        # ContentLength should still index, as the text path allows.
        data = b"%PDF-1.7 tiny"
        client = Mock()
        mock_boto3.client.return_value = client
        client.get_object.return_value = {"Body": CountingBody(data)}

        path, cleanup = _resolve_local_pdf_path("s3://bucket/ok.pdf")
        try:
            with open(path, "rb") as fh:
                assert fh.read() == data
        finally:
            cleanup()
