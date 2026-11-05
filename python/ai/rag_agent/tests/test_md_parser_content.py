# !/usr/bin/env python3
"""
Tests for MEKO-1140: content the Markdown fine-tuning path lost before
embedding, so the file indexed as COMPLETED while its text was not searchable.

- A document with no heading produced no chunks at all. Content before the
  first heading is held until a heading arrives, and in a document with none
  it never did, so the final flush emitted nothing.
- Inline code was dropped wherever it appeared: a `codespan` token keeps its
  text in "raw" and has no children, and the text extractor only read "text"
  tokens. A hex colour in a table cell came out as an empty value.

Both paths that use this parser are covered: chunk_markdown_whole_file for
`text/markdown` and chunk_pdf_whole_file for PDFs, whose Docling output is
Markdown.
"""

import sys
import types

from rag_pipeline.markdown_finetuning_chunker import chunk_markdown_whole_file
from rag_pipeline.md_parser import ParsedDocument, parse_markdown
from rag_pipeline.pdf_finetuning_chunker import chunk_pdf_whole_file

HEADINGLESS = "Note one: item 919 ships from depot 1.\n\nNote two: item 838 ships from depot 2.\n"


def texts(src: str) -> list[str]:
    return [c.text for c in parse_markdown(src, "x.md").chunks]


class TestHeadinglessDocuments:
    def test_a_document_with_no_heading_has_its_text_as_a_chunk(self):
        chunks = parse_markdown(HEADINGLESS, "notes.md").chunks
        assert len(chunks) == 1
        assert chunks[0].heading_path == []
        assert "item 919" in chunks[0].text and "item 838" in chunks[0].text

    def test_text_before_the_first_heading_still_joins_the_first_chunk(self):
        chunks = parse_markdown("Intro text.\n\n# H1\n\nBody.\n", "x.md").chunks
        assert [(c.heading_path, c.text) for c in chunks] == [
            (["H1"], "Intro text.\n\nBody.")]

    def test_an_empty_document_still_has_no_chunks(self):
        assert parse_markdown("", "x.md").chunks == []
        assert parse_markdown("\n\n   \n", "x.md").chunks == []

    def test_a_headingless_chunk_embeds_without_an_empty_section_line(self):
        chunk = parse_markdown(HEADINGLESS, "notes.md").chunks[0]
        embed = chunk.to_embed_text("Notes")
        assert "Section:" not in embed
        assert embed.startswith("Document: Notes\n")
        assert "item 919" in embed

    def test_a_chunk_with_a_heading_embeds_as_before(self):
        chunk = parse_markdown("# Palette\n\nUse six colours.\n", "x.md").chunks[0]
        assert chunk.to_embed_text("Guide") == (
            "Document: Guide\nSection: Palette\n\nUse six colours.")

    def test_a_headingless_chunk_has_metadata(self):
        doc = parse_markdown(HEADINGLESS, "notes.md")
        meta = doc.chunks[0].to_metadata(doc)
        assert meta["section_title"] == ""
        assert meta["chunk_id"].endswith("__0")


class TestInlineCode:
    def test_inline_code_in_a_paragraph_is_kept(self):
        assert texts("# T\n\nRun `make test` before you push.\n") == [
            "Run make test before you push."]

    def test_inline_code_in_a_table_cell_is_kept(self):
        src = ("# Palette\n\n| Name | Hex |\n|---|---|\n"
               "| Night | `#0d0221` |\n| Pink | `#ff2a6d` |\n")
        assert texts(src) == ["Name: Night; Hex: #0d0221\nName: Pink; Hex: #ff2a6d"]

    def test_inline_code_in_a_heading_is_kept(self):
        chunks = parse_markdown("# The `api.loop` hook\n\nDraw only.\n", "x.md").chunks
        assert chunks[0].heading_path == ["The api.loop hook"]

    def test_inline_code_inside_emphasis_and_lists_is_kept(self):
        assert texts("# T\n\n- set **`TASK_LEASE_DURATION`** to `120`\n") == [
            "set TASK_LEASE_DURATION to 120"]

    def test_fenced_code_is_still_a_code_block(self):
        chunks = parse_markdown("# T\n\n```bash\nmake test\n```\n", "x.md").chunks
        assert chunks[0].text == ""
        assert [(cb.language, cb.code) for cb in chunks[0].code_blocks] == [("bash", "make test")]


class TestBothFineTuningPaths:
    def test_markdown_path_embeds_a_headingless_file(self, tmp_path):
        f = tmp_path / "notes.md"
        f.write_text(HEADINGLESS, encoding="utf-8")
        out = list(chunk_markdown_whole_file(str(f)))
        assert len(out) == 1 and "item 919" in out[0]

    def test_markdown_path_keeps_inline_code(self, tmp_path):
        f = tmp_path / "guide.md"
        f.write_text("# Palette\n\n| Name | Hex |\n|---|---|\n| Night | `#0d0221` |\n",
                     encoding="utf-8")
        out = list(chunk_markdown_whole_file(str(f)))
        assert len(out) == 1 and "Hex: #0d0221" in out[0]

    def test_pdf_path_embeds_docling_output_with_no_heading(self, monkeypatch):
        # Docling found no headings in a single-font PDF, so its Markdown is
        # plain paragraphs. Stand in for the converter so Docling isn't loaded.
        fake = types.ModuleType("pdf_processing.docling_loader")
        fake.convert_pdf_to_markdown = lambda location: HEADINGLESS
        monkeypatch.setitem(sys.modules, "pdf_processing.docling_loader", fake)
        out = list(chunk_pdf_whole_file("s3://bucket/inventory.pdf"))
        assert len(out) == 1 and "item 838" in out[0]


def test_parsed_document_is_unchanged():
    # The fix adds no field, so callers that build ParsedDocument keep working.
    assert [f for f in ParsedDocument.__dataclass_fields__] == [
        "title", "tags", "author", "date", "slug", "source_file", "raw_frontmatter", "chunks"]
