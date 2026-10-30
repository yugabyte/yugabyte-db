# !/usr/bin/env python3
"""
Tests for MEKO-1128: Markdown over 5 MiB that the api-server accepted failed
to index, because the Markdown fine-tuning path carried its own 5 MiB cap.

The cap was there because the parser held the whole document's AST, at a cost
that depends on the document's shape rather than only its size. md_parser now
cuts the source at heading boundaries and builds one block's inline tree at a
time, so peak memory tracks the source and one cap covers both paths.

The parity tests are the point of this module: streaming must emit exactly the
chunks the whole-document walk emitted, for every shape of Markdown, or the
embeddings change.
"""

import gc
import random
import tracemalloc
from dataclasses import asdict

import mistune
import pytest

from rag_pipeline.md_parser import (
    CodeBlock,
    ParsedChunk,
    _extract_links,
    _extract_text,
    _iter_block_tokens,
    _iter_source_sections,
    _normalise,
    _table_to_block,
    parse_markdown,
    stream_markdown,
)

# A parser built the way md_parser's was, used only as the reference.
REFERENCE_PARSER = mistune.create_markdown(renderer=None, plugins=["table"])


def reference_walk(body: str) -> list[ParsedChunk]:
    """The pre-change walk: one whole-document AST, then split on headings.

    Kept verbatim so the streaming implementation has something independent to
    be equal to. If this and md_parser ever disagree, the embeddings for that
    document have changed.
    """
    ast = REFERENCE_PARSER(body) or []
    chunks: list[ParsedChunk] = []
    heading_stack: list[str] = []
    current_depth = 0
    current_text: list[str] = []
    current_codes: list[CodeBlock] = []
    current_tables: list = []
    current_links: list[str] = []

    def flush():
        if not heading_stack:
            return
        table_prose = "\n\n".join(t.to_prose() for t in current_tables)
        full_text = "\n\n".join(filter(None, [
            "\n\n".join(p for p in current_text if p.strip()),
            table_prose,
        ])).strip()
        if full_text or current_codes:
            chunks.append(ParsedChunk(
                heading_path=list(heading_stack),
                heading_depth=current_depth,
                text=full_text,
                code_blocks=list(current_codes),
                tables=list(current_tables),
                links=list(current_links),
                chunk_index=len(chunks),
            ))
        current_text.clear()
        current_codes.clear()
        current_tables.clear()
        current_links.clear()

    for node in ast:
        ntype = node.get("type")
        if ntype == "blank_line":
            continue
        elif ntype == "heading":
            flush()
            depth = node["attrs"]["level"]
            title = _extract_text(node).strip()
            heading_stack = heading_stack[:depth - 1]
            heading_stack.append(title)
            current_depth = depth
        elif ntype == "block_code":
            lang = (node.get("attrs") or {}).get("info") or ""
            current_codes.append(
                CodeBlock(language=lang, code=node.get("raw", "").rstrip())
            )
        elif ntype == "table":
            current_tables.append(_table_to_block(node))
        elif ntype in ("paragraph", "block_quote", "list"):
            current_text.append(_extract_text(node).strip())
            current_links.extend(_extract_links(node))
    flush()
    return chunks


# Each case names the structure it is there to pin down. The awkward ones are
# the places a naive text split would go wrong: a "## " line that is not a
# heading because it sits in a fence, a comment, or raw HTML.
CASES = {
    "empty": "",
    "no headings at all": "just a paragraph\n\nand another\n",
    "preamble before the first heading": "preamble\n\n# Title\n\nbody\n",
    "single h1": "# Title\n\nbody text\n",
    "nested headings": (
        "# A\n\ntext a\n\n## B\n\ntext b\n\n### C\n\ntext c\n\n## D\n\ntext d\n"
    ),
    "heading levels skipped": "# A\n\n#### D\n\ndeep\n\n## B\n\nback\n",
    "setext headings": "Title\n=====\n\nbody\n\nSub\n---\n\nmore\n",
    "frontmatter": (
        "---\ntitle: T\ntags: [a, b]\nauthor: me\ndate: 2026\n---\n\n"
        "# H\n\nbody\n"
    ),
    "hashes inside a fence": (
        "# A\n\n```python\n## not a heading\nx = 1\n```\n\nafter\n"
    ),
    "hashes in an indented code block": "# A\n\n    ## not a heading\n\nafter\n",
    "table": "# A\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\ntext\n",
    "table then heading": (
        "# A\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n## B\n\n| c |\n|---|\n| 3 |\n"
    ),
    "inline links": "# A\n\n[one](http://a.com) and [two](http://b.com)\n",
    "blockquote and list": (
        "# A\n\n> quoted\n\n- item one\n- item two with [l](http://c.com)\n"
    ),
    "headings with no body": "# A\n\n## B\n\n### C\n",
    "empty heading text": "#\n\nbody\n",
    "crlf line endings": "# A\r\n\r\nbody\r\n\r\n## B\r\n\r\nmore\r\n",
    "no trailing newline": "# A\n\nbody",
    "softbreak and hard linebreak": "# A\n\nline one\nline two  \nline three\n",
    "unicode heading": "# Ünïcödé ☃\n\ntext\n",
    "raw html block": "# A\n\n<div>raw html</div>\n\ntext\n",
    "code block only, no prose": "# A\n\n```\ncode\n```\n",
    "ref link defined in the same section": (
        "# A\n\nsee [one][r1]\n\n[r1]: http://a.com\n"
    ),
    "ref link defined in a later section": (
        "# A\n\nsee [one][r1]\n\n## B\n\nmore\n\n[r1]: http://a.com\n"
    ),
    "ref link defined in an earlier section": (
        "# A\n\n[r1]: http://a.com\n\n## B\n\nsee [one][r1]\n"
    ),
    "shortcut ref link across sections": (
        "# A\n\n[r1]\n\n## B\n\n[r1]: http://a.com\n"
    ),
    "ref def inside a fence is not a definition": (
        "# A\n\n```\n[r1]: http://evil.com\n```\n\n## B\n\nsee [one][r1]\n"
    ),
    "heading inside an html comment": (
        "# A\n\n<!--\n## not a heading\n\nstill comment\n-->\n\nafter\n"
    ),
    "heading inside a pre element": (
        "# A\n\n<pre>\n## not a heading\n\nmore\n</pre>\n\nafter\n"
    ),
    "html block closed by a blank line": (
        "# A\n\n<div>\nraw\n</div>\n\n## B\n\nbody\n"
    ),
    "hashes on the line after an html opener": (
        "# A\n\n<div>\n## inside the html block\n</div>\n\nafter\n"
    ),
    "tilde fence": "# A\n\n~~~\n## not a heading\n~~~\n\nafter\n",
    "fence closed by a longer run": (
        "# A\n\n````\n## not a heading\n```\nstill code\n````\n\nafter\n"
    ),
    "fence indented inside a list": (
        "# A\n\n- item\n\n      ```\n      ## deep\n      ```\n\nafter\n"
    ),
    "heading inside a blockquote": "# A\n\n> ## quoted heading\n> text\n",
    "unclosed fence": "# A\n\n```\n## never closed\n",
    "unclosed html comment": "# A\n\n<!--\n## never closed\n",
    "all six heading levels": (
        "# a\n\n1\n\n## b\n\n2\n\n### c\n\n3\n\n#### d\n\n4\n\n"
        "##### e\n\n5\n\n###### f\n\n6\n"
    ),
    "hash without a space is not a heading": "# A\n\n#hashtag\n\ntext\n",
    "seven hashes is not a heading": "# A\n\n####### too many\n\ntext\n",
    "closing hashes on a heading": "# A ###\n\nbody\n\n## B ##\n\nmore\n",
    "headings with no blank lines between": "# A\n## B\n### C\ntext\n",
}


def chunks_as_data(chunks) -> list[dict]:
    return [asdict(c) for c in chunks]


class TestStreamingMatchesTheWholeDocumentWalk:
    """Streaming must emit byte-identical chunks, or embeddings change."""

    @pytest.mark.parametrize("name", list(CASES))
    def test_case(self, name):
        import frontmatter

        text = CASES[name]
        expected = reference_walk(frontmatter.loads(text).content)

        _, streamed = stream_markdown(text)
        assert chunks_as_data(list(streamed)) == chunks_as_data(expected)

    @pytest.mark.parametrize("name", list(CASES))
    def test_parse_markdown_still_agrees(self, name):
        import frontmatter

        text = CASES[name]
        expected = reference_walk(frontmatter.loads(text).content)

        assert chunks_as_data(parse_markdown(text).chunks) == \
            chunks_as_data(expected)

    def test_document_fields_survive_streaming(self):
        text = CASES["frontmatter"]
        doc, chunks = stream_markdown(text, source_file="s3://b/doc.md")

        assert doc.title == "T"
        assert doc.tags == ["a", "b"]
        assert doc.author == "me"
        assert doc.source_file == "s3://b/doc.md"
        # The document is handed back before the walk runs, so the caller can
        # use doc.title while chunks are still being produced.
        assert doc.chunks == []
        assert list(chunks)

    def test_random_documents_match(self):
        import frontmatter

        frags = [
            "# H1\n\n", "## H2\n\n", "### H3\n\n", "text paragraph\n\n",
            "```py\nx=1\n## hash\n```\n\n",
            "| a | b |\n|---|---|\n| 1 | 2 |\n\n",
            "> quote\n\n", "- a\n- b\n\n", "[l](http://x.com)\n\n", "\n",
            "    indented\n\n", "Setext\n======\n\n", "#\n\n",
            "<p>html</p>\n\n", "see [one][r1]\n\n", "[r1]: http://a.com\n\n",
            "<!--\n## c\n-->\n\n", "<pre>\n## p\n</pre>\n\n",
            "<div>\n## d\n</div>\n\n", "~~~\n## t\n~~~\n\n",
            "#hashtag\n\n", "####### seven\n\n", "# A ###\n\n",
            "Sub\n---\n\n", "> ## quoted\n\n", "````\n```\n## x\n````\n\n",
            "- item\n\n      ```\n      ## deep\n      ```\n\n",
        ]
        rng = random.Random(7)
        for i in range(400):
            text = "".join(
                rng.choice(frags) for _ in range(rng.randint(1, 14))
            )
            expected = reference_walk(frontmatter.loads(text).content)
            _, streamed = stream_markdown(text)
            assert chunks_as_data(list(streamed)) == \
                chunks_as_data(expected), f"random document {i}: {text!r}"


class TestBlockTokensMatchMistune:
    """Pins the two-phase split to mistune's own behaviour.

    md_parser drives mistune's block and inline phases separately. A mistune
    upgrade that moves work between them would change chunk text silently;
    this fails instead.
    """

    @pytest.mark.parametrize("name", list(CASES))
    def test_tokens_equal_a_whole_document_parse(self, name):
        import frontmatter

        body = frontmatter.loads(CASES[name]).content
        streamed = list(_iter_block_tokens(body))
        expected = REFERENCE_PARSER(body) or []

        # blank_line tokens can fall on either side of a section boundary and
        # the walk skips them, so compare only the tokens that carry content.
        def meaningful(tokens):
            return [t for t in tokens if t.get("type") != "blank_line"]

        assert meaningful(streamed) == meaningful(expected)


class TestSourceSectionSplitting:
    """Where the source is cut, and where it deliberately is not."""

    def sections(self, text):
        return list(_iter_source_sections(_normalise(text)))

    def test_splits_at_each_column_zero_heading(self):
        assert self.sections("# A\n\nx\n\n## B\n\ny\n") == [
            "# A\n\nx\n\n", "## B\n\ny\n"
        ]

    def test_preamble_is_its_own_section(self):
        assert self.sections("intro\n\n# A\n\nx\n") == [
            "intro\n\n", "# A\n\nx\n"
        ]

    def test_no_heading_is_one_section(self):
        assert self.sections("just text\n\nmore\n") == ["just text\n\nmore\n"]

    @pytest.mark.parametrize("text,reason", [
        ("# A\n\n```\n## x\n```\n", "fenced with backticks"),
        ("# A\n\n~~~\n## x\n~~~\n", "fenced with tildes"),
        ("# A\n\n````\n```\n## x\n````\n", "fence closed by a longer run"),
        ("# A\n\n<!--\n## x\n-->\n", "html comment"),
        ("# A\n\n<pre>\n## x\n</pre>\n", "raw text element"),
        ("# A\n\n<div>\n## x\n</div>\n", "html block before a blank line"),
        ("# A\n\n    ## x\n", "indented code block"),
        ("# A\n\n> ## x\n", "inside a blockquote"),
        ("# A\n\n#hashtag\n", "no space after the hashes"),
        ("# A\n\n####### x\n", "seven hashes"),
    ])
    def test_does_not_split_on_a_non_heading(self, text, reason):
        assert len(self.sections(text)) == 1, (
            f"should not split on {reason}: {text!r}"
        )

    def test_setext_heading_is_not_a_split_point(self):
        # It is still parsed as a heading inside its section; it just does not
        # start a new one.
        assert self.sections("# A\n\nSub\n---\n\nx\n") == [
            "# A\n\nSub\n---\n\nx\n"
        ]

    def test_sections_rejoin_to_the_source(self):
        for text in CASES.values():
            s = _normalise(text)
            assert "".join(_iter_source_sections(s)) == s


class TestLaziness:
    """The walk must not build every chunk before yielding the first."""

    def test_first_chunk_arrives_before_the_document_is_walked(self):
        text = "".join(f"## S{i}\n\nbody {i}\n\n" for i in range(500))
        _, chunks = stream_markdown(text)

        first = next(iter(chunks))

        assert first.heading_path == ["S0"]
        assert first.chunk_index == 0

    def test_chunk_index_counts_emitted_chunks(self):
        text = "".join(f"## S{i}\n\nbody {i}\n\n" for i in range(5))
        _, chunks = stream_markdown(text)

        assert [c.chunk_index for c in chunks] == [0, 1, 2, 3, 4]

    def test_a_heading_with_no_content_does_not_consume_an_index(self):
        # Matches the old walk: an empty section emits no chunk, so the next
        # real one still gets index 0. The path is ["A", "B"] because a level-2
        # heading truncates the stack to one entry and there is no h1 to hold
        # slot 0, so "A" stays in it.
        _, chunks = stream_markdown("## A\n\n## B\n\nbody\n")
        emitted = list(chunks)

        assert [c.heading_path for c in emitted] == [["A", "B"]]
        assert emitted[0].chunk_index == 0


def table_document(target_bytes: int) -> str:
    """Table-heavy Markdown: the shape with the worst AST multiplier."""
    out, size, i = [], 0, 0
    while size < target_bytes:
        i += 1
        s = (f"\n## Section {i}\n\n| a | b | c |\n|---|---|---|\n"
             + "".join(f"| r{j} | v{j} | w{j} |\n" for j in range(50)))
        out.append(s)
        size += len(s)
    return "".join(out)


def peak_bytes(fn, *args) -> int:
    gc.collect()
    tracemalloc.start()
    try:
        fn(*args)
        _, peak = tracemalloc.get_traced_memory()
    finally:
        tracemalloc.stop()
    gc.collect()
    return peak


class TestSectionedMemory:
    """The reason the cap could be raised.

    Peak memory for the worst-shaped Markdown used to be a large multiple of
    the file. Measured peak RSS at a 64 MiB input, by shape: 197 MB for
    20-byte paragraphs, 229 MB for prose, 251 MB for tables, and 700 MB for a
    document with no heading after the first. Before the change, a table-heavy
    file peaked at 138.7x its size, which at 64 MiB is about 8.9 GB.

    These assertions use tracemalloc on a small document, so they are about
    the ratio between the two implementations, not the figures above.
    """

    def test_streaming_peak_is_far_below_the_whole_document_walk(self):
        text = table_document(512 * 1024)

        def streamed():
            _, chunks = stream_markdown(text)
            for _chunk in chunks:
                pass

        streaming_peak = peak_bytes(streamed)
        whole_peak = peak_bytes(reference_walk, text)

        # Measured about 9x apart on this shape; assert a wide margin so the
        # test is about the change in kind, not the exact figure.
        assert streaming_peak * 3 < whole_peak, (
            f"streaming peaked at {streaming_peak} bytes, whole-document at "
            f"{whole_peak}; the sectioned walk should be far lower"
        )

    def test_streaming_peak_is_a_small_multiple_of_the_source(self):
        text = table_document(512 * 1024)
        n = len(text.encode())

        def streamed():
            _, chunks = stream_markdown(text)
            for _chunk in chunks:
                pass

        # The whole-document walk is ~120x the source on this shape.
        assert peak_bytes(streamed) < 10 * n

    def test_the_multiplier_does_not_grow_with_the_document(self):
        """Peak per source byte stays flat, which is what bounds the cap.

        Peak still grows with the file, because the source itself is held. What
        must not grow is the multiple: that is the difference between a cap set
        by memory and a cap set by the product.
        """
        def streamed(text):
            def run():
                _, chunks = stream_markdown(text)
                for _chunk in chunks:
                    pass
            return run

        small = table_document(256 * 1024)
        large = table_document(1024 * 1024)

        small_ratio = peak_bytes(streamed(small)) / len(small.encode())
        large_ratio = peak_bytes(streamed(large)) / len(large.encode())

        assert large_ratio < small_ratio * 1.5, (
            f"peak per source byte grew from {small_ratio:.1f}x to "
            f"{large_ratio:.1f}x on a 4x larger document"
        )

    def test_the_whole_document_walk_multiplier_stays_high(self):
        """The control, so the comparisons above can't pass trivially.

        The old walk's multiplier does not grow with the document, it is just
        uniformly high: it holds every block token, every inline tree and every
        chunk at once. On this table-heavy shape it measures about 127x the
        source at both sizes, which is why a 64 MiB file could not be parsed.
        """
        small = table_document(256 * 1024)
        large = table_document(1024 * 1024)

        small_ratio = peak_bytes(reference_walk, small) / len(small.encode())
        large_ratio = peak_bytes(reference_walk, large) / len(large.encode())

        assert small_ratio > 20, f"expected a high multiplier, got {small_ratio:.1f}x"
        assert large_ratio > 20, f"expected a high multiplier, got {large_ratio:.1f}x"
