# !/usr/bin/env python3
"""
Tests for MEKO-1105: text files over 1 MiB used to fail, because each 1 MiB
slice of the S3 body was decoded on its own and the S3 response was held open
for the whole embedding run.
"""

import json
import os
import random
import tempfile
import tracemalloc
from unittest.mock import Mock, patch

import pytest

from rag_pipeline.chunk import chunk, get_splitter_for_filetype
from rag_pipeline.partition_chunk_pipeline import (
    DEFAULT_MAX_FILE_SIZE_BYTES,
    DEFAULT_MAX_MARKDOWN_FILE_SIZE_BYTES,
    _iter_lines,
    max_file_size_bytes,
    max_markdown_file_size_bytes,
    read_file_stream,
    stream_partition_and_chunk,
)

MAX_FILE_SIZE_BYTES = DEFAULT_MAX_FILE_SIZE_BYTES

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


def requests_request_exception():
    """The real RequestException, for a test that patches the requests module."""
    import requests
    return requests.exceptions.RequestException


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
    def test_stream_partition_decodes_multibyte_text(self, mock_boto3):
        # stream_partition_and_chunk reads in one call, so this covers the
        # one-shot decode of multi-byte text rather than a slice boundary.
        # Two paragraphs, each over 1 MiB of 3-byte characters.
        half = devanagari_text()
        text = f"{half}\n\n{half}"
        make_s3_object(mock_boto3, text)

        chunks = list(stream_partition_and_chunk(
            PIPELINE_ID, "s3://test-bucket/hindi.txt", {}
        ))

        assert chunks
        assert all(DEVANAGARI_KA in c for c in chunks)
        # Exact, against the same contract TestOutputUnchanged uses, so a run
        # that silently dropped most of the text can't pass.
        assert chunks == expected_chunks("s3://test-bucket/hindi.txt", text)


class TestPublicUrlFallback:
    """The no-credentials branch decodes the same way as the boto3 branch."""

    @staticmethod
    def _no_credentials(mock_boto3):
        mock_s3_client = Mock()
        mock_boto3.client.return_value = mock_s3_client
        mock_s3_client.get_object.side_effect = RuntimeError(
            "Unable to locate credentials"
        )
        return mock_s3_client

    @patch("rag_pipeline.partition_chunk_pipeline.requests")
    @patch("rag_pipeline.partition_chunk_pipeline.boto3")
    def test_public_url_decodes_across_slice_boundary(
        self, mock_boto3, mock_requests
    ):
        self._no_credentials(mock_boto3)
        text = devanagari_text()
        data = text.encode("utf-8")
        # requests is mocked, so iter_content yields byte slices the way it
        # does without a declared encoding.
        slices = [data[i:i + SLICE_SIZE]
                  for i in range(0, len(data), SLICE_SIZE)]
        response = Mock()
        response.iter_content.return_value = iter(slices)
        mock_requests.get.return_value = response
        mock_requests.exceptions.RequestException = \
            requests_request_exception()

        chunks = list(read_file_stream("s3://test-bucket/hindi.txt"))

        assert len(slices) > 1
        assert "".join(chunks) == text

    @patch("rag_pipeline.partition_chunk_pipeline.requests")
    @patch("rag_pipeline.partition_chunk_pipeline.boto3")
    def test_public_url_rejects_truncated_trailing_character(
        self, mock_boto3, mock_requests
    ):
        self._no_credentials(mock_boto3)
        truncated = (DEVANAGARI_KA * 10).encode("utf-8")[:-1]
        response = Mock()
        response.iter_content.return_value = iter([truncated])
        mock_requests.get.return_value = response
        mock_requests.exceptions.RequestException = \
            requests_request_exception()

        with pytest.raises(RuntimeError, match="utf-8"):
            list(read_file_stream("s3://test-bucket/truncated.txt"))


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

    # Meko api-server per-file KB upload caps, read from
    # meko/api_server/internal/service/free_tier.go: free 5 MB,
    # standard 50 MB, pro and above 60 MB (ProTierMaxKBFileBytes).
    LARGEST_CALLER_CAP_BYTES = 60 * 1024 * 1024

    def test_default_cap_clears_every_caller_cap(self):
        # The worker cap is an out-of-memory guard, not a product limit. If it
        # drops below what the api-server accepts, a paying user's upload
        # fails here instead of indexing.
        assert DEFAULT_MAX_FILE_SIZE_BYTES > self.LARGEST_CALLER_CAP_BYTES

    def test_an_object_at_the_largest_caller_cap_is_not_rejected(self):
        assert max_file_size_bytes() > self.LARGEST_CALLER_CAP_BYTES

    @pytest.mark.parametrize("raw,expected", [
        ("10485760", 10485760),
        ("  10485760  ", 10485760),
        ("", DEFAULT_MAX_FILE_SIZE_BYTES),
        ("not-a-number", DEFAULT_MAX_FILE_SIZE_BYTES),
        ("0", DEFAULT_MAX_FILE_SIZE_BYTES),
        ("-1", DEFAULT_MAX_FILE_SIZE_BYTES),
    ])
    def test_cap_is_configurable(self, monkeypatch, raw, expected):
        monkeypatch.setenv("MAX_FILE_SIZE_BYTES", raw)
        assert max_file_size_bytes() == expected

    def test_cap_defaults_when_unset(self, monkeypatch):
        monkeypatch.delenv("MAX_FILE_SIZE_BYTES", raising=False)
        assert max_file_size_bytes() == DEFAULT_MAX_FILE_SIZE_BYTES

    @patch("rag_pipeline.partition_chunk_pipeline.boto3")
    def test_raised_cap_lets_a_larger_object_through(
        self, mock_boto3, monkeypatch
    ):
        # A source crawled from an arbitrary bucket can hold objects over the
        # default cap; raising the env var must index them.
        monkeypatch.setenv(
            "MAX_FILE_SIZE_BYTES", str(DEFAULT_MAX_FILE_SIZE_BYTES * 2)
        )
        make_s3_object(
            mock_boto3,
            "Over the default cap.\n",
            content_length=DEFAULT_MAX_FILE_SIZE_BYTES + 1,
        )

        chunks = list(stream_partition_and_chunk(
            PIPELINE_ID, "s3://test-bucket/big.txt", {}
        ))

        assert chunks == ["Over the default cap."]

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

    @pytest.mark.parametrize("tier,cap", [
        ("free", 5 * 1024 * 1024),
        ("standard", 50 * 1024 * 1024),
        ("pro", 60 * 1024 * 1024),
    ])
    @patch("rag_pipeline.partition_chunk_pipeline.boto3")
    def test_an_upload_the_api_server_accepts_is_chunked(
        self, mock_boto3, tier, cap
    ):
        # A file at each tier's per-file upload cap must index. A worker cap
        # below one of these rejects an upload the api-server allowed.
        make_s3_object(
            mock_boto3,
            f"Uploaded on the {tier} tier.\n",
            content_length=cap,
        )

        chunks = list(stream_partition_and_chunk(
            PIPELINE_ID, f"s3://test-bucket/{tier}.txt", {}
        ))

        assert chunks == [f"Uploaded on the {tier} tier."]


class TestIterLines:
    """_iter_lines must equal str.split("\\n") without building the list."""

    @pytest.mark.parametrize("text", [
        "", "\n", "a", "a\n", "\na", "a\nb", "a\n\nb", "\n\n",
        "a\r\nb", "a b", "no newline at all", "trailing\n\n\n",
        "\n\nleading", "x" * 100,
    ])
    def test_matches_str_split(self, text):
        assert list(_iter_lines(text)) == text.split("\n")

    def test_matches_str_split_on_random_inputs(self):
        rnd = random.Random(7)
        alphabet = ["a", "b", "\n", "\n", "\r", " ", " ", " "]
        for _ in range(2000):
            text = "".join(
                rnd.choice(alphabet) for _ in range(rnd.randint(0, 14))
            )
            assert list(_iter_lines(text)) == text.split("\n"), repr(text)

    def test_does_not_retain_every_line(self):
        # str.split("\n") held an entry and a string per line until chunking
        # finished: tens of MiB on a large file, and ~500 MiB at the 64 MiB cap
        # for newline-heavy input. Lazy iteration keeps one line live.
        text = "\n" * (1024 * 1024)

        tracemalloc.start()
        try:
            base = tracemalloc.get_traced_memory()[0]
            for _ in _iter_lines(text):
                pass
            lazy_peak = tracemalloc.get_traced_memory()[1] - base
        finally:
            tracemalloc.stop()

        tracemalloc.start()
        try:
            base = tracemalloc.get_traced_memory()[0]
            for _ in iter(text.split("\n")):
                pass
            split_peak = tracemalloc.get_traced_memory()[1] - base
        finally:
            tracemalloc.stop()

        assert lazy_peak < split_peak / 100, (
            f"lazy={lazy_peak} split={split_peak}"
        )


class TestMarkdownCap:
    """Markdown keeps a lower cap, because parse_markdown costs ~23x input."""

    def test_markdown_default_is_lower_than_the_text_default(self):
        assert (DEFAULT_MAX_MARKDOWN_FILE_SIZE_BYTES
                < DEFAULT_MAX_FILE_SIZE_BYTES)

    def test_markdown_default_is_unchanged_from_master(self):
        # Raising it is MEKO-1128 and needs worker memory sized first.
        assert DEFAULT_MAX_MARKDOWN_FILE_SIZE_BYTES == 5 * 1024 * 1024

    def test_markdown_cap_is_independently_configurable(self, monkeypatch):
        monkeypatch.setenv("MAX_MARKDOWN_FILE_SIZE_BYTES", "16777216")
        monkeypatch.setenv("MAX_FILE_SIZE_BYTES", "33554432")
        assert max_markdown_file_size_bytes() == 16777216
        assert max_file_size_bytes() == 33554432

    def test_markdown_cap_falls_back_on_junk(self, monkeypatch):
        monkeypatch.setenv("MAX_MARKDOWN_FILE_SIZE_BYTES", "not-a-number")
        assert (max_markdown_file_size_bytes()
                == DEFAULT_MAX_MARKDOWN_FILE_SIZE_BYTES)

    def test_the_text_cap_does_not_move_the_markdown_cap(self, monkeypatch):
        monkeypatch.setenv("MAX_FILE_SIZE_BYTES", "67108864")
        monkeypatch.delenv("MAX_MARKDOWN_FILE_SIZE_BYTES", raising=False)
        assert (max_markdown_file_size_bytes()
                == DEFAULT_MAX_MARKDOWN_FILE_SIZE_BYTES)


class TestLineSplitting:
    """Lines split on \\n only, so chunk output doesn't change."""

    @pytest.mark.parametrize("separator,name", [
        ("\r", "carriage return"),
        ("\u2028", "U+2028 line separator"),
        ("\u2029", "U+2029 paragraph separator"),
        ("\v", "vertical tab"),
        ("\f", "form feed"),
    ])
    @patch("rag_pipeline.partition_chunk_pipeline.boto3")
    def test_other_line_breaks_stay_inside_a_paragraph(
        self, mock_boto3, separator, name
    ):
        # splitlines() and io.StringIO would break on these and split the
        # paragraph, changing chunk output. str.split("\n") must not.
        text = f"alpha{separator}beta\n\nsecond paragraph\n"
        make_s3_object(mock_boto3, text)

        chunks = list(stream_partition_and_chunk(
            PIPELINE_ID, "s3://test-bucket/doc.txt", {}
        ))

        assert f"alpha{separator}beta" in chunks, (
            f"{name} should not split the paragraph"
        )
        assert chunks == expected_chunks("s3://test-bucket/doc.txt", text)

    @pytest.mark.parametrize("text", [
        "one\ntwo\n\nthree\n",
        "one\ntwo\n\nthree",
        "trailing blank lines\n\n\n\n",
        "\n\nleading blank lines\nkeep going\n",
        "single line, no newline",
        "\n",
        "",
    ])
    @patch("rag_pipeline.partition_chunk_pipeline.boto3")
    def test_paragraph_output_matches_the_contract(self, mock_boto3, text):
        make_s3_object(mock_boto3, text)

        chunks = list(stream_partition_and_chunk(
            PIPELINE_ID, "s3://test-bucket/doc.txt", {}
        ))

        assert chunks == expected_chunks("s3://test-bucket/doc.txt", text)


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
