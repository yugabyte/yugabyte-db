from __future__ import annotations

import re
import hashlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable, Iterator, MutableMapping, Optional, Sequence

import frontmatter
import mistune
# Private names, pinned by mistune==3.4.0 in requirements.txt: the scanner
# below stands in for mistune's block parser and must apply its rules exactly.
from mistune.block_parser import _CLOSE_TAG_END, _OPEN_TAG_END
from mistune.helpers import BLOCK_TAGS, HTML_TAGNAME, PRE_TAGS
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

# Blockquote markers in front of a line: up to 3 spaces, ">", optional space,
# repeated for nesting. Stripped before looking for a fence, so a fence inside
# a quote is tracked like any other.
_QUOTE_PREFIX = re.compile(r"(?:[ ]{0,3}>[ ]?)+")
_FENCE_RUN = re.compile(r"(`{3,}|~{3,})")

# The HTML block rules, taken from mistune itself so this cannot drift from
# the parser it stands in for. mistune 3.4.0, block_parser.parse_raw_html:
# rule 1 is a PRE_TAGS element to its close tag, rules 2-5 are the comment,
# "<?", declaration and CDATA forms to their own terminators, rule 6 is a
# BLOCK_TAGS open or close tag to the next blank line, and rule 7 is any
# other complete tag alone on its line to the next blank line, but only when
# it does not interrupt a paragraph. All of them require at most 3 spaces of
# indent; at 4 the line is indented code.
_HTML_START = re.compile(
    r"[ ]{0,3}(?:<(?P<close>/?)(?P<tag>" + HTML_TAGNAME + r")"
    r"|(?P<comment><!--)|(?P<pi><\?)|(?P<cdata><!\[CDATA\[)"
    r"|(?P<decl><![A-Z]))"
)
_HTML_RAW_TAGS = frozenset(PRE_TAGS)
_HTML_BLOCK_TAGS = frozenset(BLOCK_TAGS)
_HTML_COMMENT_END = re.compile(r"-->")
_HTML_PI_END = re.compile(r"\?>")
_HTML_CDATA_END = re.compile(r"\]\]>")
_HTML_DECL_END = re.compile(r">")

def _indent_width(prefix: str) -> int:
    """Columns of indent in `prefix`, a tab advancing to the next 4-stop.

    Four columns makes a line indented code rather than a container marker.
    mistune agrees for tabs: "```\\n\\t```\\n# H1\\n" parses to one block_code
    and no heading on 3.4.0, so a tab-indented fence run is not a closer.
    """
    width = 0
    for ch in prefix:
        width = (width // 4 + 1) * 4 if ch == "\t" else width + 1
    return width


class _Fence:
    """An open fenced code block, as the scanner sees it.

    `quotes` is how many blockquote markers preceded the opener and `indent`
    its indent inside them. `nested` is True when the fence sits in a
    container - behind a quote marker, or indented 4 or more under a list
    item. mistune tracks those fences too, but an earlier version of this
    scanner did not, so a blank-line cut could land inside one and the rest
    of the code re-parsed as Markdown.
    """

    __slots__ = ("run", "quotes", "indent", "nested")

    def __init__(self, run: str, quotes: int, indent: int) -> None:
        self.run = run
        self.quotes = quotes
        self.indent = indent
        self.nested = quotes > 0 or indent >= 4


def _split_quote_prefix(line: str) -> tuple[int, str]:
    """Return (number of blockquote markers, the line after them)."""
    match = _QUOTE_PREFIX.match(line)
    if not match:
        return 0, line
    return match.group(0).count(">"), line[match.end():]


def _fence_opens(line: str) -> Optional[_Fence]:
    """The fence `line` opens, or None.

    Looks past blockquote markers and any indent, so a fence inside a quote
    or a list item is found as well as a top-level one. A backtick fence's
    info string may not contain a backtick, so ``` a`b is not a fence at all.
    Getting that wrong pairs the scanner's fences differently from mistune's
    and moves a split point into a code block.
    """
    quotes, rest = _split_quote_prefix(line)
    stripped = rest.lstrip(" \t")
    match = _FENCE_RUN.match(stripped)
    if not match:
        return None
    run = match.group(1)
    if run[0] == "`" and "`" in stripped[match.end():]:
        return None
    indent = _indent_width(rest[:len(rest) - len(stripped)])
    return _Fence(run, quotes, indent)


def _fence_closes(line: str, fence: _Fence) -> bool:
    """True when `line` closes `fence`.

    A closer is a run of the fence's character, at least as long as the
    opener, with nothing else on the line, behind the same number of quote
    markers. For a top-level fence it may be indented at most 3 spaces: at 4,
    mistune does not accept it as a closer, so treating it as one leaves the
    scanner outside a code block that mistune still has open. For a nested
    fence the closer's indent may differ from the opener's by up to 3 either
    way, which is CommonMark's rule inside a container.

    The indent rule is also what stops a stray fence run inside indented code
    from closing a real fence opened elsewhere, which would invert the
    scanner's fence state.
    """
    quotes, rest = _split_quote_prefix(line)
    if quotes != fence.quotes:
        return False
    stripped = rest.lstrip(" \t")
    indent = _indent_width(rest[:len(rest) - len(stripped)])
    if fence.nested:
        if abs(indent - fence.indent) > 3:
            return False
    elif indent > 3:
        return False
    bare = stripped.rstrip()
    return (len(bare) >= len(fence.run)
            and set(bare) == {fence.run[0]})


# A blockquote line and a list-item line, as the hold below needs them.
_QUOTE_LINE = re.compile(r"[ ]{0,3}>")
_LIST_ITEM_LINE = re.compile(r"[ ]{0,3}(?:[-*+]|\d{1,9}[.)])(?:[ \t]|$)")
# Lines that end a paragraph on their own, which a rule-7 HTML block may then
# follow. A thematic break needs three or more of one character; an "="
# underline, or a "-" one too short to be a break, is a setext underline only
# if a paragraph line precedes it, and is otherwise paragraph text itself.
_THEMATIC_BREAK = re.compile(
    r"[ ]{0,3}(?:(?:-[ \t]*){3,}|(?:\*[ \t]*){3,}|(?:_[ \t]*){3,})$"
)
_SETEXT_SHORT = re.compile(r"[ ]{0,3}(?:=+|-{1,2})[ \t]*$")
# A GFM table delimiter row: cells of dashes with optional colons, at least
# one pipe. Only a table if the line above was the header row, i.e. text.
_TABLE_DELIMITER = re.compile(
    r"[ ]{0,3}\|?[ \t]*:?-+:?[ \t]*(?:\|[ \t]*:?-+:?[ \t]*)*\|?[ \t]*$"
)


class _BlockScanner:
    """Tracks which lines sit at the document's top level.

    Used by `_iter_source_sections` and `_iter_section_pieces`, so the two
    cuts agree about what is inside a block and neither lands in one.
    (`_seed_ref_links` once scanned lines too; it now block-parses the pieces
    and lets mistune decide, so it has no view of its own.)

    Besides the top-level verdict, `blocked` is set on the lines where a cut
    is top-level but still unsafe. mistune 3.4.0 has one construct whose
    parse depends on the block *after* the next blank line: a list item
    directly after a blockquote line, with no blank between. Parsed alone,
    "> q\\n- a\\n\\n" gives [block_quote, list]; followed by a heading, rule,
    fence or quote it gives [list, block_quote, ...], and the chunk text
    comes out in a different order. Only a list triggers it, only when it
    starts inside the quote's run of lines, and only the one block after the
    blank is consulted - verified by parsing each variant with and without
    the trailing block. So after such a run the next blank line and the
    first block after it are both held: cutting at either would hand mistune
    a different tail.
    """

    def __init__(self) -> None:
        self._fence: _Fence | None = None
        self._html_close: re.Pattern[str] | None = None
        self._html_until_blank = False
        # True when the previous line left a paragraph open, which is what a
        # rule-7 HTML block is not allowed to interrupt. Verified against
        # mistune 3.4.0 for each kind of previous line: open after paragraph
        # text, a list item or a quote line; closed after a heading, a
        # thematic break, a setext underline, a table, a fence, an HTML block,
        # indented code or a blank.
        self._in_text = False
        self._in_table = False
        # The blockquote hold described on the class.
        self._in_quote_run = False
        self._hold_next_block = False
        self.blocked = False

    def feed(self, line: str) -> bool:
        """Consume `line`; True only if it is a plain top-level line.

        False while inside, opening or closing a fenced code block or an HTML
        block, so the caller never acts on such a line. A blockquote needs no
        tracking of its own for the split: a split point requires a preceding
        blank line, which closes one. It does need the hold on the class.

        Every judgement here is deliberately conservative: reporting a line as
        not top-level only costs a cut point, and so memory, while reporting
        one wrongly moves a cut into a block and changes chunk text. The
        residual cases where a wrong report is still possible are listed on
        `_iter_section_pieces`.
        """
        self.blocked = False

        if self._fence is not None:
            if _fence_closes(line, self._fence):
                self._fence = None
            return False

        if self._html_close is not None:
            if self._html_close.search(line):
                self._html_close = None
            return False

        blank = not line.strip()

        if self._hold_next_block:
            # Held: the blank after a quote-then-list run, and the first block
            # after it, which is what decides how mistune orders that run.
            self.blocked = True
            if not blank:
                self._hold_next_block = False

        if self._html_until_blank:
            if blank:
                # The blank line ends the HTML block and is itself top-level,
                # so a heading after it is a real split point.
                self._html_until_blank = False
                self._in_text = False
                self._in_quote_run = False
                return True
            return False

        if blank:
            # A blank line is itself top-level: _iter_source_sections and
            # _iter_section_pieces both key off exactly these.
            self._in_text = False
            self._in_table = False
            self._in_quote_run = False
            return True

        fence = _fence_opens(line)
        if fence:
            self._fence = fence
            self._in_text = False
            self._in_table = False
            self._in_quote_run = False
            return False

        if self._starts_html_block(line):
            self._in_text = False
            self._in_table = False
            self._in_quote_run = False
            return False

        if _QUOTE_LINE.match(line):
            self._in_quote_run = True
        elif self._in_quote_run and _LIST_ITEM_LINE.match(line):
            self._hold_next_block = True

        self._in_text = self._leaves_paragraph_open(line)
        return True

    def _leaves_paragraph_open(self, line: str) -> bool:
        """Whether a paragraph is still open after this top-level line.

        Decides whether a rule-7 HTML block on the next line is a block or
        paragraph text. Getting it wrong in either direction can move a cut:
        if a fence with a blank line inside follows the tag, treating the tag
        as HTML leaves that fence untracked and inverts the fence state, and
        treating it as text when mistune starts an HTML block has the scanner
        tracking a fence that mistune does not.
        """
        was_open = self._in_text
        stripped = line.lstrip(" \t")
        indent = _indent_width(line[:len(line) - len(stripped)])

        if self._in_table:
            if "|" in line:
                return False
            self._in_table = False

        if indent >= 4:
            # Indented: a lazy continuation if a paragraph is open, else
            # indented code. Either way the state does not change.
            return was_open

        if _ATX_HEADING.match(line) or _THEMATIC_BREAK.match(line):
            return False
        if _SETEXT_SHORT.match(line):
            # An underline closes the paragraph above it. With none above,
            # the line is a paragraph of its own.
            return not was_open
        if was_open and "|" in line and _TABLE_DELIMITER.match(line):
            # Header row then delimiter row: a table, which continues while
            # the lines keep their pipes and leaves no paragraph open.
            self._in_table = True
            return False
        return True

    def _starts_html_block(self, line: str) -> bool:
        """Apply mistune's HTML block rules to `line`, arming any terminator."""
        match = _HTML_START.match(line)
        if not match:
            return False
        end = match.end()

        # Rules 2-5 run to their own terminator, not to a blank line.
        for group, closer in (
            ("comment", _HTML_COMMENT_END),
            ("pi", _HTML_PI_END),
            ("cdata", _HTML_CDATA_END),
            ("decl", _HTML_DECL_END),
        ):
            if match.group(group):
                # A one-line block closes on its own line, so only arm the
                # terminator when it is not already here.
                if not closer.search(line, end):
                    self._html_close = closer
                return True

        tag = match.group("tag").lower()
        is_close = bool(match.group("close"))

        # Rule 1: a raw-text element runs to its own close tag.
        if not is_close and tag in _HTML_RAW_TAGS:
            closer = re.compile(r"</" + tag + r">", re.IGNORECASE)
            if not closer.search(line, end):
                self._html_close = closer
            return True

        # Rule 6: a known block tag, open or close, runs to a blank line.
        if tag in _HTML_BLOCK_TAGS:
            self._html_until_blank = True
            return True

        # Rule 7: any other tag, but only a complete one alone on its line,
        # and only where it does not interrupt a paragraph. "<b>Note</b> text"
        # is a paragraph, and so is "<custom>" straight after a text line.
        if self._in_text:
            return False
        tag_end = _CLOSE_TAG_END if is_close else _OPEN_TAG_END
        if tag_end.match(line, end):
            self._html_until_blank = True
            return True
        return False


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
        if (top_level and after_blank and not scanner.blocked
                and _ATX_HEADING.match(line)):
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

    The pass walks the same sections and pieces as the real one, so no single
    parse call sees more than ``_MAX_FORCED_PIECE_BYTES`` plus a line, like
    everything else, and it is skipped entirely unless the source holds
    something that could be a definition.
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
# further, first at top-level blank lines and then, past
# _MAX_FORCED_PIECE_BYTES, mid-block. The blank-line cut leaves the parse
# unchanged; the forced one does not. _iter_section_pieces has the details.
_MAX_SECTION_BYTES = 1024 * 1024  # 1 MiB

# A blank line is the only cut that preserves the parse, and one enormous block
# contains none: a single 5 MiB table measures 85.9x and a single 5 MiB list
# 45.5x, both unbounded, which at the 64 MiB cap is gigabytes again. Past this
# size the cut is taken at the next top-level line instead, accepting a changed
# parse for that block. See _iter_section_pieces.
_MAX_FORCED_PIECE_BYTES = 2 * _MAX_SECTION_BYTES  # 2 MiB

# A setext heading underline. On its own it is a paragraph ("===") or a
# thematic break ("---"); it is only a heading because of the paragraph above
# it, and the heading's text is that whole paragraph, so a forced cut must not
# land anywhere in a paragraph that ends this way.
_SETEXT_UNDERLINE = re.compile(r"[ ]{0,3}(?:=+|-+)[ \t]*$")
# A blank line as the scanner sees one: whitespace-only counts.
_BLANK_LINE = re.compile(r"\n[ \t\v\f]*\n")


def _paragraph_run(s: str, pos: int) -> tuple[int, bool]:
    """The span from `pos` a forced cut must treat as one: (its end, setext?).

    Scans the non-blank lines from `pos`. If one of them, after the first, is
    a setext underline, the paragraph before it is a heading whose text is
    the whole paragraph, so the span ends just past the underline and the
    second value is True: no cut may land inside it. A setext underline ends
    the paragraph by itself, with no blank line needed, which is why the scan
    stops at the first one rather than at the next blank. Otherwise the span
    runs to the next blank line and the second value is False.

    A lone "---" after a blank is a thematic break, not an underline; the
    "after the first" rule covers that, and treating a doubtful "---" as an
    underline only costs a cut point. `end` is an index just past a newline,
    so the caller can cache the answer until it has moved beyond it. Works on
    indices, since the span may be most of a 64 MiB document.
    """
    match = _BLANK_LINE.search(s, pos)
    span_end = match.start() + 1 if match else len(s)
    line_start = s.find("\n", pos, span_end)
    if line_start == -1:
        return span_end, False
    line_start += 1  # skip the first line: an underline needs a title
    while line_start < span_end:
        eol = s.find("\n", line_start, span_end)
        if eol == -1:
            eol = span_end
        # match(s, pos, endpos) treats endpos as the end of the string, so the
        # pattern's "$" lands at the end of this line without slicing it out.
        if _SETEXT_UNDERLINE.match(s, line_start, eol):
            return min(eol + 1, len(s)), True
        line_start = eol + 1
    return span_end, False


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

    No cut is taken where the scanner reports a line as inside a fenced code
    block or an HTML block, including a fence nested in a list item or a
    blockquote. A fence does not need cutting anyway: it is one token holding
    raw text and measures 4.0x at any size, and cutting one would turn code
    into Markdown. Nor is a forced cut taken before a line whose meaning
    depends on the line above it: a setext underline, which would drop a
    heading, or an indented line, which may be a heading inside a list item
    that a cut would promote to top level. Either would change
    ``heading_path`` and ``chunk_index`` for every later chunk; skipping costs
    at most one more line of piece size.

    Known divergences from a whole-document parse, all confined to a section
    over the threshold, and all to one block's text rather than the heading
    sequence:

    - the forced cut above;
    - a loose list straddling a blank-line cut becomes two lists;
    - an indented code block containing a blank line becomes two code blocks;
    - a forced cut inside a paragraph turns its soft line break into a
      paragraph break.

    And two residual ways a cut can still land somewhere mistune would not
    put a block boundary, both needing a construct the scanner approximates:
    a fence inside a blockquote whose closer is missing, where mistune ends
    the fence with the quote and the scanner waits for a closer (a missed
    cut, so memory only); and a rule-7 HTML block, which may not interrupt a
    paragraph, judged against the previous line being text rather than
    mistune's full notion of an open paragraph.
    """
    if len(section) <= _MAX_SECTION_BYTES:
        yield section
        return

    scanner = _BlockScanner()
    start = 0
    # The end of the paragraph run a forced cut would land in, and whether
    # that run closes with a setext underline. Computed once per run, so a
    # single huge paragraph is scanned forward once, not once per candidate.
    run_end = -1
    run_is_setext = False
    for pos, eol in _iter_line_spans(section):
        line = section[pos:eol]
        if not scanner.feed(line) or scanner.blocked:
            # Inside a fence or an HTML block, or held after a quote-then-list
            # run: never a cut point.
            continue
        size = pos - start
        if not line.strip() and size >= _MAX_SECTION_BYTES:
            # Cut after the blank line, so the next piece starts on content.
            yield section[start:eol + 1]
            start = eol + 1
        elif size >= _MAX_FORCED_PIECE_BYTES and line[:1] not in (" ", "\t"):
            # No blank line arrived in time. Cut before this line, which is
            # mid-block, rather than let the block grow without bound. Not
            # before an indented line, which takes its meaning from the line
            # above, and not anywhere inside a paragraph that ends in a setext
            # underline: the heading's text is the whole paragraph, so a cut
            # anywhere in it shortens the heading and changes heading_path and
            # chunk_index for every later chunk.
            if pos >= run_end:
                run_end, run_is_setext = _paragraph_run(section, pos)
            if run_is_setext:
                continue
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

    Four things bound it. The source is cut into heading-delimited sections;
    an oversized section is cut again at top-level blank lines; a single
    block too big even for that is cut mid-block, past
    ``_MAX_FORCED_PIECE_BYTES``; and each token's inline tree is built as it
    is yielded and dropped straight after. The first three only cut where
    the scanner says the source is top-level. The blank-line cut leaves the
    parse unchanged and the forced cut does not; `_iter_section_pieces` lists
    what it changes.

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