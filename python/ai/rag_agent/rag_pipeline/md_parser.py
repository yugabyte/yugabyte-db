from __future__ import annotations

import re
import hashlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable, Iterator, MutableMapping, Optional, Sequence

import frontmatter
import mistune
from langchain_core.documents import Document

@dataclass
class CodeBlock:
    language: str
    code: str


@dataclass
class TableBlock:
    headers: list[str]
    rows: list[list[str]]

    def to_prose(self) -> str:
        """Convert table to key: value prose — embeds cleanly vs raw markdown."""
        lines = []
        for row in self.rows:
            parts = [f"{h}: {v}" for h, v in zip(self.headers, row)]
            lines.append("; ".join(parts))
        return "\n".join(lines)


@dataclass
class ParsedChunk:
    heading_path:  list[str]
    heading_depth: int
    text:          str
    code_blocks:   list[CodeBlock] = field(default_factory=list)
    tables:        list[TableBlock] = field(default_factory=list)
    links:         list[str]       = field(default_factory=list)
    chunk_index:   int = 0

    def to_embed_text(self, doc_title: str = "") -> str:
        breadcrumb = " > ".join(self.heading_path)
        lines = []
        if doc_title:
            lines.append(f"Document: {doc_title}")
        lines.append(f"Section: {breadcrumb}")
        lines.append("")
        lines.append(self.text)
        for cb in self.code_blocks:
            lang = cb.language or "code"
            lines.append(f"\n[{lang} example]\n{cb.code[:300]}")
        return "\n".join(lines).strip()

    def to_metadata(self, doc: "ParsedDocument") -> dict[str, Any]:
        return {
            "chunk_id":      self._chunk_id(doc.slug),
            "doc_id":        doc.slug,
            "source_file":   doc.source_file,
            "title":         doc.title,
            "section_title": self.heading_path[-1] if self.heading_path else "",
            "heading_path":  self.heading_path,
            "heading_depth": self.heading_depth,
            "chunk_index":   self.chunk_index,
            "doc_length":    doc.total_chunks,
            "tags":          doc.tags,
            "author":        doc.author,
            "date":          doc.date,
            "has_code":      bool(self.code_blocks),
            "code_langs":    list({cb.language for cb in self.code_blocks if cb.language}),
            "has_table":     bool(self.tables),
            "links":         self.links,
            "token_estimate": len(self.text.split()),   # rough; swap in tiktoken
            "content_hash":  self._content_hash(),
        }

    def _chunk_id(self, doc_slug: str) -> str:
        slug = re.sub(r"[^a-z0-9]+", "-", self.heading_path[-1].lower()).strip("-")
        return f"{doc_slug}__{slug}__{self.chunk_index}"

    def _content_hash(self) -> str:
        return hashlib.sha256(self.text.encode()).hexdigest()[:16]


@dataclass
class ParsedDocument:
    title:           str
    tags:            list[str]
    author:          str
    date:            str
    slug:            str
    source_file:     str
    raw_frontmatter: dict[str, Any] 
    chunks:          list[ParsedChunk] = field(default_factory=list)

    @property
    def total_chunks(self) -> int:
        return len(self.chunks)

def _extract_text(node: dict) -> str:
    if node.get("type") == "text":
        return node.get("raw", "")
    if node.get("type") == "softbreak":
        return " "
    if node.get("type") == "linebreak":
        return "\n"
    text = ""
    for child in node.get("children") or []:
        text += _extract_text(child)
    return text


def _extract_links(node: dict) -> list[str]:
    links = []
    if node.get("type") == "link":
        href = (node.get("attrs") or {}).get("url", "")
        if href:
            links.append(href)
    for child in node.get("children") or []:
        links.extend(_extract_links(child))
    return links


def _table_to_block(node: dict) -> TableBlock:
    headers: list[str] = []
    rows: list[list[str]] = []
    for child in node.get("children") or []:
        if child["type"] == "table_head":
            for cell in child.get("children") or []:
                headers.append(_extract_text(cell).strip())
        elif child["type"] == "table_body":
            for row_node in child.get("children") or []:
                row = [
                    _extract_text(cell).strip()
                    for cell in row_node.get("children") or []
                ]
                rows.append(row)
    return TableBlock(headers=headers, rows=rows)

_MD_PARSER = mistune.create_markdown(renderer=None, plugins=["table"])


def _inline_parse(tok: dict, state: Any) -> dict:
    """Run mistune's inline phase over one block token, in place.

    This is the body of ``mistune.Markdown._iter_render`` for a single token.
    It is reproduced here so the inline phase can be driven one block at a
    time; ``test_md_streaming`` asserts the result equals ``_MD_PARSER(body)``
    so a mistune upgrade that changes this is caught.
    """
    if "children" in tok:
        tok["children"] = [_inline_parse(c, state) for c in tok["children"]]
    elif "text" in tok:
        text = tok.pop("text")
        # Same strip set as mistune: don't touch emsp or other unicode spaces.
        tok["children"] = _MD_PARSER.inline(text.strip(" \r\n\t\f"), state.env)
    return tok


# A column-0 ATX heading: where the source is cut into sections. Requires a
# space or end of line after the hashes, so "#hashtag" is not one, and no
# leading space, so a heading nested in a list or quote is not one either.
# Setext headings ("Title\n=====") are deliberately not split points; they are
# parsed normally inside whichever section contains them, and an oversized
# section is bounded by _iter_section_pieces instead.
_ATX_HEADING = re.compile(r"#{1,6}(?:[ \t]|$)")
_FENCE_OPEN = re.compile(r"[ ]{0,3}(`{3,}|~{3,})")
_HTML_RAW_OPEN = re.compile(
    r"[ ]{0,3}<(?:pre|script|style|textarea)\b", re.IGNORECASE
)
_HTML_RAW_CLOSE = re.compile(
    r"</(?:pre|script|style|textarea)>", re.IGNORECASE
)
# Any other block-level HTML opener, which CommonMark ends at a blank line.
_HTML_OPEN = re.compile(r"[ ]{0,3}<[A-Za-z/!?]")
# Openers that run to a specific terminator instead of a blank line. "#" is
# valid PHP comment syntax and "## " can appear in a declaration or CDATA
# block, so treating these as blank-line-terminated would split inside them.
_HTML_TERMINATED = (
    ("<!--", re.compile(r"-->")),
    ("<![CDATA[", re.compile(r"\]\]>")),
    ("<?", re.compile(r"\?>")),
)
_HTML_DECL_OPEN = re.compile(r"[ ]{0,3}<![A-Za-z]")
_HTML_DECL_CLOSE = re.compile(r">")
# Four spaces of indent makes a line indented code, not a container marker.
# A tab counts too: it advances to the next 4-column tab stop, and mistune
# does not accept a tab-indented line as a fence closer either (verified on
# 3.4.0: "```\n\t```\n# H1\n" parses to one block_code, no heading).
_OVER_INDENTED = re.compile(r"(?:[ ]{4}|[ ]{0,3}\t)")


def _fence_opens(line: str) -> Optional[str]:
    """The fence run `line` opens, or None.

    A backtick fence's info string may not contain a backtick, so ``` a`b is
    not a fence at all. Getting that wrong pairs the scanner's fences
    differently from mistune's and moves a split point into a code block.
    """
    match = _FENCE_OPEN.match(line)
    if not match:
        return None
    run = match.group(1)
    if run[0] == "`" and "`" in line[match.end():]:
        return None
    return run


def _fence_closes(line: str, fence: str) -> bool:
    """True when `line` closes a fence opened with the run `fence`.

    A closer is a run of the same character, at least as long as the opener,
    with nothing else on the line, indented at most 3 spaces. The indent
    matters: at 4 spaces mistune does not accept the line as a closer, so
    treating it as one leaves the scanner outside a code block that mistune
    still has open.
    """
    if _OVER_INDENTED.match(line):
        return False
    stripped = line.strip()
    return (len(stripped) >= len(fence)
            and set(stripped) == {fence[0]})


class _BlockScanner:
    """Tracks which lines sit at the document's top level.

    Shared by the two line scans so they cannot disagree about what counts as
    inside a container: `_iter_source_sections` must not cut there, and
    `_seed_ref_links` must not read a link definition out of it. Keeping one
    state machine is the point - an earlier version tracked fences in both and
    HTML in only one, which seeded definitions out of HTML comments.
    """

    def __init__(self) -> None:
        self._fence: str | None = None
        self._html_close: re.Pattern[str] | None = None
        self._html_until_blank = False

    def feed(self, line: str) -> bool:
        """Consume `line`; True only if it is a plain top-level line.

        False while inside, opening or closing a fenced code block or any
        flavour of HTML block, so the caller never acts on such a line. A
        blockquote needs no tracking: a split point requires a preceding blank
        line, which closes one.

        Every judgement here is deliberately conservative: reporting a line as
        not top-level only costs a cut point, and so memory, while reporting
        one wrongly moves a cut into a block and changes chunk text.
        """
        if self._fence is not None:
            if _fence_closes(line, self._fence):
                self._fence = None
            return False

        if self._html_close is not None:
            if self._html_close.search(line):
                self._html_close = None
            return False

        if self._html_until_blank:
            if not line.strip():
                self._html_until_blank = False
            return False

        if not line.strip():
            # A blank line is itself top-level: _iter_source_sections and
            # _iter_section_pieces both key off exactly these.
            return True

        fence_run = _fence_opens(line)
        if fence_run:
            self._fence = fence_run
            return False

        for marker, closer in _HTML_TERMINATED:
            index = line.find(marker)
            if index != -1 and not line[:index].strip():
                # A one-line block closes on its own line, so only arm the
                # terminator when it is not already here.
                if not closer.search(line, index + len(marker)):
                    self._html_close = closer
                return False

        raw_open = _HTML_RAW_OPEN.match(line)
        if raw_open:
            if not _HTML_RAW_CLOSE.search(line, raw_open.end()):
                self._html_close = _HTML_RAW_CLOSE
            return False

        decl_open = _HTML_DECL_OPEN.match(line)
        if decl_open:
            if not _HTML_DECL_CLOSE.search(line, decl_open.end()):
                self._html_close = _HTML_DECL_CLOSE
            return False

        if _HTML_OPEN.match(line):
            self._html_until_blank = True
            return False

        return True


def _iter_line_spans(s: str) -> Iterator[tuple[int, int]]:
    """Yield (start, end_of_line_exclusive) for each line of `s`."""
    pos = 0
    n = len(s)
    while pos < n:
        eol = s.find("\n", pos)
        if eol == -1:
            eol = n
        yield pos, eol
        pos = eol + 1


def _iter_source_sections(s: str) -> Iterator[str]:
    """Cut normalised Markdown into sections at column-0 ATX headings.

    Each section after the first starts with its heading line, and the first
    is whatever preceded the first heading. Splitting here is what keeps peak
    memory flat: the block parser is handed one section at a time instead of
    the whole document.

    A heading only counts when a top-level blank line precedes it. That rule
    carries the correctness of this whole approach, so it is worth stating
    why. A blank line ends every CommonMark block except the ones the scanner
    already tracks, so after one, no container is open that mistune would
    keep a following heading inside. Without the rule, a heading after a
    blockquote line became a split point, while mistune keeps it in the
    quote; and in the general case mistune's token *order* can depend on what
    follows, so cutting mid-construct is not something a pre-split scanner can
    get right. A heading with no blank line before it is therefore left in
    place, which costs a cut point and nothing else: `_iter_section_pieces`
    still bounds the section.
    """
    scanner = _BlockScanner()
    start = 0
    # The start of the source counts as preceded by a blank line.
    after_blank = True

    for pos, eol in _iter_line_spans(s):
        line = s[pos:eol]
        top_level = scanner.feed(line)
        if top_level and after_blank and _ATX_HEADING.match(line):
            if pos > start:
                yield s[start:pos]
            start = pos
        # Only a top-level blank line separates blocks; one inside a fence or
        # an HTML block does not, and feed() reports those as not top-level.
        after_blank = top_level and not line.strip()

    if start < len(s):
        yield s[start:]


def _normalise(body: str) -> str:
    """Line separators and trailing newline, exactly as Markdown.parse does."""
    s = body.replace("\r\n", "\n").replace("\r", "\n")
    if not s.endswith("\n"):
        s += "\n"
    return s


def _seed_ref_links(s: str, env: MutableMapping[str, Any]) -> None:
    """Register the document's link reference definitions up front.

    A whole-document parse collects every ``[label]: url`` before any inline
    text is parsed, so a definition below a use still resolves. Parsing one
    section at a time would only see definitions already passed, hence this
    pass, which populates ``env["ref_links"]`` before the real walk starts.

    Rather than guess which lines are definitions, this block-parses the
    source a second time and throws the tokens away, keeping only what mistune
    registered in ``env``. Guessing was wrong in both directions. Picking lines
    that merely look like definitions seeded one that cannot interrupt a
    paragraph, so ``Title`` followed by ``[r]: http://x`` resolved a reference
    mistune leaves as literal text; and matching only at column 0-3 missed a
    definition behind a blockquote or list marker, or one whose destination is
    on the next line.

    The pass walks the same sections and pieces as the real one, so it is
    bounded by ``_MAX_SECTION_BYTES`` like everything else, and it is skipped
    entirely unless the source holds something that could be a definition.
    """
    if "]:" not in s:
        return

    for section in _iter_source_sections(s):
        for piece in _iter_section_pieces(section):
            state = _MD_PARSER.block.state_cls()
            state.env = env
            state.process(_normalise(piece))
            for hook in _MD_PARSER.before_parse_hooks:
                hook(_MD_PARSER, state)
            _MD_PARSER.block.parse(state)
            # Only env matters here; the tokens are rebuilt by the real walk.
            state.tokens.clear()


# Largest section handed to the block parser in one call. A section is parsed
# whole, and the block token list for one section is what sets peak memory, so
# a document with no column-0 ATX heading after its start would otherwise cost
# the old whole-document multiple: table-heavy Markdown measures about 92x its
# size, roughly 6 GB at the 64 MiB read cap. Over this size a section is cut
# further at top-level blank lines, which bounds the token list without
# changing which chunks are produced, since chunks are flushed on headings
# only.
_MAX_SECTION_BYTES = 1024 * 1024  # 1 MiB

# A blank line is the only cut that preserves the parse, and one enormous block
# contains none: a single 5 MiB table measures 85.9x and a single 5 MiB list
# 45.5x, both unbounded, which at the 64 MiB cap is gigabytes again. Past this
# size the cut is taken at the next top-level line instead, accepting a changed
# parse for that block. See _iter_section_pieces.
_MAX_FORCED_PIECE_BYTES = 2 * _MAX_SECTION_BYTES  # 2 MiB


def _iter_section_pieces(section: str) -> Iterator[str]:
    """Cut an oversized section so no one parse call sees too much at once.

    Only sections over ``_MAX_SECTION_BYTES`` are cut, so an ordinary document
    is parsed exactly as before.

    The preferred cut is a top-level blank line, which always ends a
    CommonMark block. For the shapes that make a section huge - many tables,
    many paragraphs - each piece then parses to the same block tokens as the
    whole section would, and the chunk is unchanged.

    One enormous block has no blank line to cut at, and is the case that
    matters most: a single 5 MiB table measures 85.9x its size and a single
    5 MiB list 45.5x, neither bounded by a blank-line rule. Past
    ``_MAX_FORCED_PIECE_BYTES`` the cut is therefore taken at the next
    top-level line, which does change how that block parses - the rows after
    the cut lose their header row and become a paragraph, and a list becomes
    two lists. That is accepted, because the alternative is a worker that runs
    out of memory on input the api-server accepted.

    This changes the number of rows stored, not just their text. The chunk
    count out of this walk is unchanged, since chunks flush on headings only,
    but ``split_embed_text`` then cuts each chunk at ``MAX_EMBED_CHARS``, and
    the prose rendering of the cut rows is shorter than the table rendering.
    Measured on the scale stack with a 4 MiB single table: 231 rows before,
    203 after, with every row's text still present.

    A cut is never taken inside a fenced code block or an HTML block, both
    because the scanner reports those lines as not top-level and because they
    do not need it: a fence is one token holding raw text and measures 4.0x at
    any size. Cutting one would turn code into Markdown.

    Known divergences from a whole-document parse, both confined to a section
    over the threshold: a loose list straddling a blank-line cut becomes two
    lists, and an indented code block containing a blank line becomes two code
    blocks.
    """
    if len(section) <= _MAX_SECTION_BYTES:
        yield section
        return

    scanner = _BlockScanner()
    start = 0
    for pos, eol in _iter_line_spans(section):
        line = section[pos:eol]
        if not scanner.feed(line):
            # Inside a fence or an HTML block: never a cut point.
            continue
        size = pos - start
        if not line.strip() and size >= _MAX_SECTION_BYTES:
            # Cut after the blank line, so the next piece starts on content.
            yield section[start:eol + 1]
            start = eol + 1
        elif size >= _MAX_FORCED_PIECE_BYTES:
            # No blank line arrived in time. Cut before this line, which is
            # mid-block, rather than let the block grow without bound.
            yield section[start:pos]
            start = pos

    if start < len(section):
        yield section[start:]


def _iter_block_tokens(body: str) -> Iterator[dict]:
    """Yield block tokens one at a time, each with its inline tree attached.

    ``_MD_PARSER(body)`` builds every block token for the document and then
    every token's inline tree, and holds all of it at once. That is what makes
    peak memory a large multiple of the source, by a factor that depends on
    the Markdown's shape: measured at 5 MiB input, 3.6x for fenced code, 8.9x
    for prose, 43.2x for 20-byte paragraphs and 127.7x for tables.

    Three things bound it. The source is cut into heading-delimited sections,
    an oversized section is cut again at top-level blank lines, and each
    token's inline tree is built as it is yielded and dropped straight after.
    The second of those is what covers a document with no column-0 ATX heading
    after its start, which is a single section and would otherwise cost the
    full whole-document multiple.

    One ``env`` is shared across sections and pieces, so link reference
    definitions still resolve document-wide.
    """
    s = _normalise(body)

    state = _MD_PARSER.block.state_cls()
    env = state.env
    _seed_ref_links(s, env)

    for section in _iter_source_sections(s):
        for piece in _iter_section_pieces(section):
            state = _MD_PARSER.block.state_cls()
            state.env = env
            state.process(_normalise(piece))

            for hook in _MD_PARSER.before_parse_hooks:
                hook(_MD_PARSER, state)
            _MD_PARSER.block.parse(state)
            for hook in _MD_PARSER.before_render_hooks:
                hook(_MD_PARSER, state)

            tokens = state.tokens
            for i, tok in enumerate(tokens):
                yield _inline_parse(tok, state)
                # Drop the finished token, inline tree and all, so the walk
                # below never holds more than one block's worth of it.
                tokens[i] = None


def _document_from_frontmatter(
    content: str, source_file: str
) -> tuple[ParsedDocument, str]:
    """Split frontmatter from body and build the chunkless document."""
    post = frontmatter.loads(content)
    meta: dict[str, Any] = post.metadata
    body: str = post.content

    raw_title = str(meta.get("title", ""))
    slug = str(meta.get(
        "slug",
        re.sub(r"[^a-z0-9]+", "-", raw_title.lower()).strip("-") or "doc"
    ))

    doc = ParsedDocument(
        title           = raw_title,
        tags            = list(meta.get("tags") or []),
        author          = str(meta.get("author", "")),
        date            = str(meta.get("date", "")),
        slug            = slug,
        source_file     = source_file,
        raw_frontmatter = dict(meta),
    )
    return doc, body


def _iter_chunks(body: str) -> Iterator[ParsedChunk]:
    """Walk the block tokens, splitting on headings, yielding as we go."""
    heading_stack:    list[str] = []
    current_depth:    int = 0
    current_text:     list[str] = []
    current_codes:    list[CodeBlock] = []
    current_tables:   list[TableBlock] = []
    current_links:    list[str] = []
    chunk_index:      int = 0

    def flush() -> ParsedChunk | None:
        nonlocal chunk_index
        if not heading_stack:
            # No clear() on this path, deliberately: content before the first
            # heading stays buffered and lands in the first chunk.
            return None
        # Inline table prose into the text block so it's searchable
        table_prose = "\n\n".join(t.to_prose() for t in current_tables)
        full_text = "\n\n".join(filter(None, [
            "\n\n".join(p for p in current_text if p.strip()),
            table_prose,
        ])).strip()

        chunk = None
        if full_text or current_codes:
            chunk = ParsedChunk(
                heading_path  = list(heading_stack),
                heading_depth = current_depth,
                text          = full_text,
                code_blocks   = list(current_codes),
                tables        = list(current_tables),
                links         = list(current_links),
                chunk_index   = chunk_index,
            )
            chunk_index += 1

        current_text.clear()
        current_codes.clear()
        current_tables.clear()
        current_links.clear()
        return chunk

    for node in _iter_block_tokens(body):
        ntype = node.get("type")

        if ntype == "blank_line":
            continue

        elif ntype == "heading":
            pending = flush()
            if pending is not None:
                yield pending
            depth = node["attrs"]["level"]
            title = _extract_text(node).strip()
            heading_stack = heading_stack[:depth - 1]
            heading_stack.append(title)
            current_depth = depth

        elif ntype == "block_code":
            lang = (node.get("attrs") or {}).get("info") or ""
            code = node.get("raw", "").rstrip()
            current_codes.append(CodeBlock(language=lang, code=code))

        elif ntype == "table":
            current_tables.append(_table_to_block(node))

        elif ntype in ("paragraph", "block_quote", "list"):
            current_text.append(_extract_text(node).strip())
            current_links.extend(_extract_links(node))

    pending = flush()
    if pending is not None:
        yield pending


def stream_markdown(
    content: str, source_file: str = ""
) -> tuple[ParsedDocument, Iterator[ParsedChunk]]:
    """Parse the frontmatter now; yield chunks lazily.

    The returned document carries the document-level fields with an empty
    ``chunks`` list, and the iterator produces the same chunks
    :func:`parse_markdown` would, in the same order, without ever holding them
    all at once. Use this on the ingestion path, where chunks are embedded and
    discarded one at a time; use :func:`parse_markdown` when the whole
    document is genuinely needed.

    ``doc.total_chunks`` stays 0 while the iterator runs, so it cannot feed
    :meth:`ParsedChunk.to_metadata`'s ``doc_length``. That is why
    :class:`MekoMarkdownParser`, which needs the metadata, still collects.
    """
    doc, body = _document_from_frontmatter(content, source_file)
    return doc, _iter_chunks(body)


def parse_markdown(content: str, source_file: str = "") -> ParsedDocument:
    """Parse a whole Markdown document, chunks included."""
    doc, chunks = stream_markdown(content, source_file)
    doc.chunks.extend(chunks)
    return doc


def parse_file(path: str | Path) -> ParsedDocument:
    p = Path(path)
    return parse_markdown(p.read_text(encoding="utf-8"), source_file=str(p))


# ─────────────────────────────────────────────────────────────────────────────
# Size cap for embed-ready chunks
# ─────────────────────────────────────────────────────────────────────────────
#
# Heading-scoped chunks have no inherent size bound: a long section between two
# headings becomes one large chunk, which can exceed the embedding provider's
# per-request limit (Bedrock ``amazon.titan-embed-text-v1`` = 8192 tokens) and
# fail the whole document. ``split_embed_text`` splits an oversized block into a
# few parts on paragraph boundaries; blocks that already fit pass through as-is.

# ~4 chars per token, so 24000 chars stays comfortably under the 8192-token cap.
MAX_EMBED_CHARS = 24000


def split_embed_text(text: str, max_chars: int = MAX_EMBED_CHARS) -> list[str]:
    """Split an embed-ready block into a few parts if it exceeds ``max_chars``.

    Returns ``[text]`` unchanged when it fits (the common case). Oversized text
    is packed into the fewest parts that each fit, breaking on nearby paragraph
    boundaries; a single paragraph larger than the budget is hard-split by
    characters. The ``Document:``/``Section:`` breadcrumb header is repeated on
    each part so every piece keeps its context.
    """
    if len(text) <= max_chars:
        return [text]

    # Keep the breadcrumb header (the first line block) to repeat on every part.
    header = ""
    body = text
    head, sep, rest = text.partition("\n\n")
    if sep and (head.startswith("Document:") or head.startswith("Section:")):
        header, body = head, rest

    budget = max(1, max_chars - len(header) - 2)
    parts: list[str] = []
    current = ""
    for para in re.split(r"\n{2,}", body):
        if not para.strip():
            continue
        candidate = f"{current}\n\n{para}" if current else para
        if len(candidate) <= budget:
            current = candidate
            continue
        if current:
            parts.append(current)
            current = ""
        if len(para) <= budget:
            current = para
        else:  # one paragraph bigger than the budget -> hard character split
            for i in range(0, len(para), budget):
                parts.append(para[i:i + budget])
    if current:
        parts.append(current)

    return [f"{header}\n\n{p}" if header else p for p in parts] or [text]


# ─────────────────────────────────────────────────────────────────────────────
# LangChain-compatible wrapper
# ─────────────────────────────────────────────────────────────────────────────


PAGE_CONTENT_EMBED = "embed"
PAGE_CONTENT_RAW = "raw"
_VALID_PAGE_CONTENT_FORMATS = frozenset({PAGE_CONTENT_EMBED, PAGE_CONTENT_RAW})


class MekoMarkdownParser:
    """Markdown parser/splitter compatible with the LangChain text-splitter API.

    Wraps :func:`parse_markdown` so its rich, structure-aware chunks can flow
    into any LangChain pipeline that expects ``langchain_core.documents.Document``
    objects (vector stores, retrievers, ``BaseDocumentTransformer`` chains, etc.).

    Each emitted ``Document`` corresponds to one heading-scoped section of the
    source markdown. Frontmatter, code blocks, tables (rendered as prose), and
    links are all preserved either inside ``page_content`` (when
    ``page_content_format="embed"``) or in the metadata.

    The class mirrors the public surface of LangChain splitters so it can be
    used as a drop-in alternative to ``MarkdownHeaderTextSplitter`` /
    ``ExperimentalMarkdownSyntaxTextSplitter``:

    * ``split_text(text) -> list[Document]``
    * ``create_documents(texts, metadatas=None) -> list[Document]``
    * ``split_documents(documents) -> list[Document]``
    * ``transform_documents(documents, **kwargs) -> Sequence[Document]``
    * ``atransform_documents(documents, **kwargs) -> Sequence[Document]``

    Example:
        ```python
        parser = MekoMarkdownParser()
        docs = parser.split_text(open("README.md").read())
        # docs is list[langchain_core.documents.Document]
        ```
    """

    def __init__(
        self,
        *,
        page_content_format: str = PAGE_CONTENT_EMBED,
        source_metadata_key: str = "source",
        return_each_line: bool = False,
        include_raw_frontmatter: bool = False,
        extra_metadata: dict[str, Any] | None = None,
    ) -> None:
        """Configure the parser.

        Args:
            page_content_format: ``"embed"`` (default) emits the full
                embedding-ready text produced by :meth:`ParsedChunk.to_embed_text`
                (document title + heading breadcrumb + body + code previews).
                ``"raw"`` emits only the chunk body text — closer to what the
                stock LangChain ``MarkdownHeaderTextSplitter`` produces.
            source_metadata_key: When consuming existing ``Document`` objects via
                :meth:`split_documents`, the value at this metadata key is used
                as the ``source_file`` for parsing. Defaults to ``"source"``,
                which matches the convention used by LangChain document
                loaders.
            return_each_line: If ``True``, each non-empty line of a chunk is
                emitted as its own ``Document`` (sharing the chunk's metadata).
                Provided for parity with LangChain's
                ``MarkdownHeaderTextSplitter``.
            include_raw_frontmatter: If ``True``, attach the parsed YAML
                frontmatter dict under the ``raw_frontmatter`` metadata key.
                Off by default since the flat fields (``title``, ``tags``,
                ``author``, ``date``) are already promoted into metadata.
            extra_metadata: Optional dict merged into every emitted
                ``Document``'s metadata. Useful for run-level fields such as
                ``tenant_id`` or ``ingest_run_id``. Per-chunk keys take
                precedence on collision.
        """
        if page_content_format not in _VALID_PAGE_CONTENT_FORMATS:
            raise ValueError(
                f"page_content_format must be one of "
                f"{sorted(_VALID_PAGE_CONTENT_FORMATS)}, "
                f"got {page_content_format!r}"
            )
        self.page_content_format = page_content_format
        self.source_metadata_key = source_metadata_key
        self.return_each_line = return_each_line
        self.include_raw_frontmatter = include_raw_frontmatter
        self.extra_metadata: dict[str, Any] = dict(extra_metadata or {})

    # ── Pass-throughs to the underlying parser ───────────────────────────────

    def parse(self, text: str, source_file: str = "") -> ParsedDocument:
        """Parse markdown into the rich :class:`ParsedDocument` representation."""
        return parse_markdown(text, source_file=source_file)

    def parse_file(self, path: str | Path) -> ParsedDocument:
        """Read and parse a markdown file from disk."""
        return parse_file(path)

    # ── LangChain-compatible API ─────────────────────────────────────────────

    def split_text(self, text: str, source_file: str = "") -> list[Document]:
        """Split markdown text into a list of LangChain ``Document`` objects.

        Args:
            text: The full markdown text (frontmatter optional).
            source_file: Optional path of the source file. Recorded in each
                emitted ``Document``'s metadata as ``source_file``. The base
                LangChain ``split_text`` signature is ``(self, text)``; this
                extra positional argument is keyword-defaulted so calling code
                that targets the LangChain contract works unchanged.

        Returns:
            One ``Document`` per heading-scoped chunk produced by
            :func:`parse_markdown`. When ``return_each_line=True`` is set, each
            non-empty line of a chunk is emitted as its own ``Document``.
        """
        parsed = self.parse(text, source_file=source_file)
        return self._documents_from_parsed(parsed, base_metadata=None)

    def create_documents(
        self,
        texts: list[str],
        metadatas: list[dict[str, Any]] | None = None,
    ) -> list[Document]:
        """Create ``Document`` objects from a list of markdown texts.

        Mirrors :meth:`langchain_text_splitters.TextSplitter.create_documents`.
        Per-input metadata is merged into every chunk produced from that input;
        chunk-level metadata wins on key collisions so the structural fields
        (``heading_path``, ``content_hash``, ...) are never overwritten.
        """
        if metadatas is not None and len(metadatas) != len(texts):
            raise ValueError(
                f"metadatas length ({len(metadatas)}) must match texts length "
                f"({len(texts)})"
            )
        meta_iter: list[dict[str, Any]] = (
            list(metadatas) if metadatas is not None else [{} for _ in texts]
        )

        documents: list[Document] = []
        for text, meta in zip(texts, meta_iter):
            base_metadata = dict(meta or {})
            source_file = str(base_metadata.get(self.source_metadata_key, "") or "")
            parsed = self.parse(text, source_file=source_file)
            documents.extend(
                self._documents_from_parsed(parsed, base_metadata=base_metadata)
            )
        return documents

    def split_documents(
        self, documents: Iterable[Document]
    ) -> list[Document]:
        """Split each input ``Document``'s ``page_content`` as markdown.

        Mirrors :meth:`langchain_text_splitters.TextSplitter.split_documents`.
        The input documents' metadata is propagated onto every output document
        produced from them.
        """
        texts: list[str] = []
        metadatas: list[dict[str, Any]] = []
        for doc in documents:
            texts.append(doc.page_content)
            metadatas.append(dict(doc.metadata or {}))
        return self.create_documents(texts, metadatas=metadatas)

    # ── BaseDocumentTransformer-compatible API ───────────────────────────────

    def transform_documents(
        self, documents: Sequence[Document], **kwargs: Any
    ) -> Sequence[Document]:
        """``BaseDocumentTransformer.transform_documents`` shim."""
        del kwargs
        return self.split_documents(documents)

    async def atransform_documents(
        self, documents: Sequence[Document], **kwargs: Any
    ) -> Sequence[Document]:
        """Async variant of :meth:`transform_documents` (no real async work)."""
        del kwargs
        return self.split_documents(documents)

    # ── Internals ────────────────────────────────────────────────────────────

    def _documents_from_parsed(
        self,
        parsed: ParsedDocument,
        base_metadata: dict[str, Any] | None,
    ) -> list[Document]:
        out: list[Document] = []
        for chunk in parsed.chunks:
            page_content = self._render_page_content(parsed, chunk)
            metadata = self._build_metadata(parsed, chunk, base_metadata)

            if self.return_each_line:
                for line in page_content.splitlines():
                    if line.strip():
                        out.append(
                            Document(page_content=line, metadata=dict(metadata))
                        )
            else:
                out.append(Document(page_content=page_content, metadata=metadata))
        return out

    def _render_page_content(
        self, parsed: ParsedDocument, chunk: ParsedChunk
    ) -> str:
        if self.page_content_format == PAGE_CONTENT_EMBED:
            return chunk.to_embed_text(parsed.title)
        return chunk.text

    def _build_metadata(
        self,
        parsed: ParsedDocument,
        chunk: ParsedChunk,
        base_metadata: dict[str, Any] | None,
    ) -> dict[str, Any]:
        metadata: dict[str, Any] = {}
        if self.extra_metadata:
            metadata.update(self.extra_metadata)
        if base_metadata:
            metadata.update(base_metadata)
        # Chunk metadata is authoritative — its structural keys
        # (chunk_id, content_hash, heading_path, ...) must not be overwritten
        # by upstream metadata blobs.
        metadata.update(chunk.to_metadata(parsed))
        if self.include_raw_frontmatter:
            metadata["raw_frontmatter"] = dict(parsed.raw_frontmatter)
        return metadata