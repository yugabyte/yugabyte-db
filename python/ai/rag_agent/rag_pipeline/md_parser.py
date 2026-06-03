from __future__ import annotations

import re
import hashlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable, Sequence

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


def parse_markdown(content: str, source_file: str = "") -> ParsedDocument:
    # ── Step 1: split frontmatter from body ───────────────────────────────────
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

    # ── Step 2: parse body to AST ─────────────────────────────────────────────
    ast: list[dict] = _MD_PARSER(body) or []

    # ── Step 3: walk AST, split on headings ───────────────────────────────────
    heading_stack:    list[str] = []
    current_depth:    int = 0
    current_text:     list[str] = []
    current_codes:    list[CodeBlock] = []
    current_tables:   list[TableBlock] = []
    current_links:    list[str] = []

    def flush() -> None:
        if not heading_stack:
            return
        # Inline table prose into the text block so it's searchable
        table_prose = "\n\n".join(t.to_prose() for t in current_tables)
        full_text = "\n\n".join(filter(None, [
            "\n\n".join(p for p in current_text if p.strip()),
            table_prose,
        ])).strip()

        if full_text or current_codes:
            chunk = ParsedChunk(
                heading_path  = list(heading_stack),
                heading_depth = current_depth,
                text          = full_text,
                code_blocks   = list(current_codes),
                tables        = list(current_tables),
                links         = list(current_links),
                chunk_index   = len(doc.chunks),
            )
            doc.chunks.append(chunk)

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
            code = node.get("raw", "").rstrip()
            current_codes.append(CodeBlock(language=lang, code=code))

        elif ntype == "table":
            current_tables.append(_table_to_block(node))

        elif ntype in ("paragraph", "block_quote", "list"):
            current_text.append(_extract_text(node).strip())
            current_links.extend(_extract_links(node))

    flush()
    return doc


def parse_file(path: str | Path) -> ParsedDocument:
    p = Path(path)
    return parse_markdown(p.read_text(encoding="utf-8"), source_file=str(p))


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