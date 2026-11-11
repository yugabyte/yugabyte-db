# !/usr/bin/env python3
"""
Tests for content-aware chunking of plain-text uploads (MEKO-1236).

Covers the profile and its classes, the two strategies, and how
stream_partition_and_chunk and the embed path apply them behind
ENABLE_CONTENT_AWARE_CHUNKING.
"""

import os
import tempfile
from unittest.mock import MagicMock, Mock, patch

import pytest

from rag_pipeline.content_profile import (
    CHUNKER_VERSION,
    DUMP,
    PROSE,
    PROSE_MERGE_CHARS,
    RECORDS,
    merge_paragraphs,
    profile_text,
    split_dump_objects,
)
from rag_pipeline.partition_chunk_pipeline import stream_partition_and_chunk

PIPELINE_ID = 1
FLAG = "ENABLE_CONTENT_AWARE_CHUNKING"


def _dump_object(name, kind="TABLE", toc=False):
    toc_line = f"-- TOC entry 21{len(name)} (class 1259 OID 16385)\n" if toc else ""
    return (
        "--\n"
        f"{toc_line}"
        f"-- Name: {name}; Type: {kind}; Schema: public; Owner: yugabyte\n"
        "--\n"
        "\n"
        f"CREATE TABLE public.{name} (\n"
        "    id integer NOT NULL,\n"
        "\n"
        "    name text\n"
        ")\n"
        "SPLIT INTO 3 TABLETS;\n"
        "\n"
        "\n"
        f"ALTER TABLE public.{name} OWNER TO yugabyte;\n"
        "\n"
    )


DUMP_PREAMBLE = (
    "--\n"
    "-- YSQL database dump\n"
    "--\n"
    "\n"
    "SET statement_timeout = 0;\n"
    "SET client_encoding = 'UTF8';\n"
    "\n"
)

SCHEMA_DUMP = DUMP_PREAMBLE + "".join(
    _dump_object(f"t{i}", toc=(i % 2 == 0)) for i in range(5)
)

LOG = "".join(
    f"2026-11-10T07:{i // 60:02d}:{i % 60:02d}Z INFO worker: batch {i} done\n"
    for i in range(40)
)

CSV = "id,name,city,score\n" + "".join(
    f"{i},user{i},city{i},{i * 3}\n" for i in range(40)
)

PROSE_TEXT = "\n\n".join(
    f"Paragraph {i} says something short about YugabyteDB." for i in range(30)
)


def _write(content, suffix=".txt"):
    with tempfile.NamedTemporaryFile(
        mode="w", delete=False, suffix=suffix
    ) as f:
        f.write(content)
        return f.name


@pytest.fixture
def flag_on(monkeypatch):
    monkeypatch.setenv(FLAG, "true")


@pytest.fixture
def flag_off(monkeypatch):
    monkeypatch.delenv(FLAG, raising=False)


def _chunks(content, chunk_args=None, suffix=".txt"):
    path = _write(content, suffix)
    plans = []
    try:
        chunks = list(stream_partition_and_chunk(
            PIPELINE_ID, path, chunk_args or {}, on_plan=plans.append
        ))
    finally:
        os.unlink(path)
    assert len(plans) == 1
    return chunks, plans[0]


class TestProfile:
    def test_schema_dump(self):
        p = profile_text(SCHEMA_DUMP)
        assert p.content_class == DUMP
        assert p.dump_object_headers == 5

    def test_two_headers_are_not_a_dump(self):
        text = DUMP_PREAMBLE + _dump_object("a") + _dump_object("b")
        assert profile_text(text).content_class != DUMP

    def test_log(self):
        p = profile_text(LOG)
        assert p.content_class == RECORDS
        assert p.timestamp_share == 1.0

    def test_csv(self):
        p = profile_text(CSV)
        assert p.content_class == RECORDS
        assert p.delimited_share == 1.0

    def test_prose(self):
        assert profile_text(PROSE_TEXT).content_class == PROSE

    def test_prose_with_commas_is_not_delimited(self):
        text = "\n".join(
            f"Line {i}, which has" + ", a clause" * (i % 4) + "." for i in range(40)
        )
        assert profile_text(text).content_class == PROSE

    def test_short_file_is_prose(self):
        assert profile_text(LOG.splitlines()[0]).content_class == PROSE

    def test_empty(self):
        p = profile_text("")
        assert p.content_class == PROSE
        assert p.sampled_lines == 0


class TestMergeParagraphs:
    def test_merges_up_to_limit(self):
        merged = list(merge_paragraphs(["a" * 100, "b" * 100, "c" * 100], 250))
        assert merged == ["a" * 100 + "\n\n" + "b" * 100, "c" * 100]

    def test_exact_fit_merges(self):
        merged = list(merge_paragraphs(["a" * 149, "b" * 149], 300))
        assert merged == ["a" * 149 + "\n\n" + "b" * 149]

    def test_long_paragraph_stands_alone(self):
        merged = list(merge_paragraphs(["a", "b" * 400, "c"], 300))
        assert merged == ["a", "b" * 400, "c"]

    def test_no_merged_piece_exceeds_limit(self):
        paragraphs = [str(i) * (i % 70 + 1) for i in range(200)]
        merged = list(merge_paragraphs(paragraphs, 300))
        assert all(len(m) <= 300 for m in merged)
        assert "\n\n".join(merged) == "\n\n".join(paragraphs)


class TestSplitDumpObjects:
    def test_one_block_per_object_plus_preamble(self):
        blocks = list(split_dump_objects(SCHEMA_DUMP))
        assert len(blocks) == 6
        assert blocks[0].startswith("--\n-- YSQL database dump")
        for i, block in enumerate(blocks[1:]):
            assert block.startswith("--\n")
            assert f"-- Name: t{i}; Type: TABLE" in block
            assert f"ALTER TABLE public.t{i} OWNER" in block

    def test_toc_entry_stays_with_its_header(self):
        blocks = list(split_dump_objects(SCHEMA_DUMP))
        assert blocks[1].splitlines()[1].startswith("-- TOC entry")

    def test_header_on_first_line(self):
        text = "-- Name: a; Type: TABLE; Schema: public; Owner: x\nCREATE TABLE a();"
        assert list(split_dump_objects(text)) == [text]

    def test_data_for_name_header(self):
        text = (
            _dump_object("a")
            + "--\n-- Data for Name: a; Type: TABLE DATA; Schema: public; Owner: x\n--\n\n"
            + "COPY public.a (id, name) FROM stdin;\n1\tx\n\\.\n"
        )
        blocks = list(split_dump_objects(text))
        assert len(blocks) == 2
        assert blocks[1].startswith("--\n-- Data for Name: a")

    def test_keeps_all_text(self):
        blocks = list(split_dump_objects(SCHEMA_DUMP))

        def squash(s):
            return "".join(s.split())
        assert squash("".join(blocks)) == squash(SCHEMA_DUMP)


class TestStreamPartitionAndChunk:
    def test_flag_off_keeps_paragraph_chunks_and_reports_profile(self, flag_off):
        chunks, plan = _chunks(PROSE_TEXT)
        assert len(chunks) == 30
        assert plan["strategy"] == "paragraph"
        assert plan["chunker_version"] == CHUNKER_VERSION
        assert plan["profile"]["content_class"] == PROSE

    def test_prose_merges(self, flag_on):
        chunks, plan = _chunks(PROSE_TEXT)
        assert plan["strategy"] == "prose_merge"
        assert len(chunks) < 30
        assert all(len(c) <= PROSE_MERGE_CHARS for c in chunks)
        assert "\n\n".join(chunks) == PROSE_TEXT

    def test_merge_capped_by_chunk_size(self, flag_on):
        args = {"args": '{"chunk_size": 120, "chunk_overlap": 0}'}
        chunks, _ = _chunks(PROSE_TEXT, args)
        assert all(len(c) <= 120 for c in chunks)
        assert max(len(c) for c in chunks) > 60  # did merge

    def test_dump_packs_whole_objects(self, flag_on):
        chunks, plan = _chunks(SCHEMA_DUMP)
        assert plan["strategy"] == "dump_objects"
        assert len(chunks) < 6
        assert all(len(c) <= 1000 for c in chunks)
        for i in range(5):
            owner = [c for c in chunks if f"-- Name: t{i}; Type: TABLE" in c]
            assert len(owner) == 1
            assert f"ALTER TABLE public.t{i} OWNER" in owner[0]

    def test_dump_paragraph_chunks_when_off(self, flag_off):
        chunks, _ = _chunks(SCHEMA_DUMP)
        assert len(chunks) > 6

    def test_large_dump_object_is_still_split(self, flag_on):
        big = _dump_object("big").replace(
            "    name text\n", "".join(f"    c{i} text,\n" for i in range(200))
        )
        chunks, _ = _chunks(SCHEMA_DUMP + big)
        assert all(len(c) <= 1000 for c in chunks)
        assert len(chunks) > 7

    @pytest.mark.parametrize("content", [LOG, CSV])
    def test_records_unchanged(self, monkeypatch, content):
        monkeypatch.delenv(FLAG, raising=False)
        off, _ = _chunks(content)
        monkeypatch.setenv(FLAG, "true")
        on, plan = _chunks(content)
        assert on == off
        assert plan["strategy"] == "paragraph"
        assert plan["profile"]["content_class"] == RECORDS

    def test_named_splitter_skips_profiling(self, flag_on):
        args = {"splitter": "recursive_character",
                "args": '{"chunk_size": 1000, "chunk_overlap": 100}'}
        chunks, plan = _chunks(PROSE_TEXT, args)
        assert len(chunks) == 30
        assert plan == {"chunker_version": CHUNKER_VERSION, "strategy": "paragraph"}

    def test_only_text_plain_is_profiled(self, flag_on):
        chunks, plan = _chunks(PROSE_TEXT, suffix=".json")
        assert len(chunks) == 30
        assert "profile" not in plan

    def test_deterministic(self, flag_on):
        assert _chunks(SCHEMA_DUMP + PROSE_TEXT) == _chunks(SCHEMA_DUMP + PROSE_TEXT)

    def test_on_plan_is_optional(self, flag_on):
        path = _write(PROSE_TEXT)
        try:
            assert list(stream_partition_and_chunk(PIPELINE_ID, path, None))
        finally:
            os.unlink(path)


class TestRecordChunkingPlan:
    def test_merges_into_metadata_snapshot(self):
        from db.active_pipeline_tracking import PipelineTracking

        cursor = MagicMock()
        conn = MagicMock()
        conn.cursor.return_value = cursor
        pool = MagicMock()
        pool.get_connection.return_value = conn
        with patch("db.active_pipeline_tracking.ConnectionPool", return_value=pool):
            tracking = PipelineTracking()

        plan = {"chunker_version": 1, "strategy": "prose_merge"}
        assert tracking.record_chunking_plan("p1", plan) is True

        sql = " ".join(cursor.execute.call_args[0][0].split())
        assert "SET metadata_snapshot = metadata_snapshot || %s::jsonb" in sql
        assert cursor.execute.call_args[0][1] == (
            '{"chunking": {"chunker_version": 1, "strategy": "prose_merge"}}', "p1"
        )
        conn.commit.assert_called_once()


class TestEmbedRecordsPlan:
    @patch("embeddings.embed.PipelineTracking")
    @patch("embeddings.embed.PDFProcessor")
    @patch("embeddings.embed.OpenAIEmbeddings")
    def test_plan_recorded_and_failure_ignored(
        self, mock_oai_cls, mock_pdf, mock_pt, flag_on
    ):
        from embeddings.embed import EmbeddingsGenerator

        embedder = Mock()
        embedder.embed_documents.side_effect = lambda texts: [[0.1]] * len(texts)
        mock_oai_cls.return_value = embedder
        tracking = mock_pt.return_value
        tracking.record_chunking_plan.side_effect = RuntimeError("db down")

        gen = EmbeddingsGenerator(
            embedding_model="text-embedding-ada-002",
            embedding_model_params={"model": "m", "dimensions": 1},
        )
        path = _write(SCHEMA_DUMP)
        try:
            results = list(gen.generate_embeddings(PIPELINE_ID, path))
        finally:
            os.unlink(path)

        assert len(results) == 2
        tracking.record_chunking_plan.assert_called_once()
        pid, plan = tracking.record_chunking_plan.call_args[0]
        assert pid == PIPELINE_ID
        assert plan["strategy"] == "dump_objects"
