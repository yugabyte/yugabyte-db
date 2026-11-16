"""Pick a plain-text chunking strategy from what the document contains.

The paragraph chunker cuts a text file at every blank line and chunks each
paragraph on its own. On a schema dump, which is mostly short blank-line
separated blocks, that gives chunks of around 100 characters and one embedding
call per chunk. Merging small paragraphs helps, but the best merge
size depends on the content. Measured against today's chunker (recall@5, Titan
v1, 300 synthetic questions per corpus): merging to 300 characters gained 6.7
points on docs and 5.3 on prose and lost 20.7 on a schema dump; merging to
1,000 did the reverse. So the strategy is chosen per document:

- PROSE: merge consecutive paragraphs up to PROSE_MERGE_CHARS.
- DUMP: cut at SQL dump objects (the "-- Name: X; Type: Y" header blocks
  pg_dump and ysql_dump write), then pack whole objects up to the splitter's
  chunk size, so no object is split across chunks unless it alone is too big.
  One chunk per object was not enough: constraints and indexes are objects of
  a line or two each. Not measured yet: whether keeping objects whole beats
  the plain 1,000-character merge on recall.
- RECORDS: logs and delimited exports. Kept on the paragraph chunker, since
  they have few blank lines and merging barely changed their chunk counts.

The rules are deterministic on purpose. Chunk ids hash a chunk's position and
text (db.yugabytedb_vector_store.chunk_id), so a resumed upload only skips the
chunks an earlier attempt stored if it chunks the same way.
"""

import re
from dataclasses import asdict, dataclass
from typing import Any, Dict, Iterable, List

# Bump when any rule or constant here changes what chunks a file produces.
CHUNKER_VERSION = 1

PROSE = "prose"
DUMP = "dump"
RECORDS = "records"

PROSE_MERGE_CHARS = 300

DUMP_MIN_OBJECT_HEADERS = 3
RECORDS_MIN_LINES = 20
RECORDS_TIMESTAMP_SHARE = 0.5
RECORDS_DELIMITED_SHARE = 0.8
# Line statistics read this many non-blank lines from the start of the file.
# A fixed prefix keeps profiling cheap on a 64 MiB file and still
# deterministic.
PROFILE_SAMPLE_LINES = 5000

# "-- Name: users; Type: TABLE; Schema: public; Owner: yugabyte", and the
# "-- Data for Name: ..." form for COPY blocks.
_DUMP_HEADER = re.compile(
    r"^-- (?:Data for )?Name: [^\n]*; Type: [^\n]*$", re.MULTILINE
)

_TIMESTAMP = re.compile(
    r"^\[?(?:"
    r"\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}"            # ISO 8601
    r"|[A-Z][a-z]{2} [ \d]\d \d{2}:\d{2}:\d{2}"    # syslog
    r"|[IWEF]\d{4} \d{2}:\d{2}:\d{2}"              # glog
    r")"
)

_DELIMITERS = (",", "\t", "|", ";")


@dataclass(frozen=True)
class ContentProfile:
    content_class: str
    dump_object_headers: int
    sampled_lines: int
    timestamp_share: float
    delimited_share: float

    def to_metadata(self) -> Dict[str, Any]:
        return asdict(self)


def _non_blank_prefix(text: str, limit: int) -> List[str]:
    lines = []
    start = 0
    while len(lines) < limit:
        end = text.find("\n", start)
        line = text[start:] if end == -1 else text[start:end]
        if line.strip():
            lines.append(line)
        if end == -1:
            break
        start = end + 1
    return lines


def _delimited_share(lines: List[str]) -> float:
    """Share of lines with the file's most common count (>= 2) of one delimiter.

    A CSV or TSV export has the same field count on nearly every line; prose
    has commas, but not the same number of them line after line.
    """
    best = 0
    for delim in _DELIMITERS:
        counts: Dict[int, int] = {}
        for line in lines:
            n = line.count(delim)
            if n >= 2:
                counts[n] = counts.get(n, 0) + 1
        if counts:
            best = max(best, max(counts.values()))
    return best / len(lines) if lines else 0.0


def profile_text(text: str) -> ContentProfile:
    headers = len(_DUMP_HEADER.findall(text))
    lines = _non_blank_prefix(text, PROFILE_SAMPLE_LINES)
    sampled = len(lines)
    timestamp_share = (
        sum(1 for line in lines if _TIMESTAMP.match(line)) / sampled
        if sampled else 0.0
    )
    delimited_share = _delimited_share(lines)

    if headers >= DUMP_MIN_OBJECT_HEADERS:
        content_class = DUMP
    elif sampled >= RECORDS_MIN_LINES and (
        timestamp_share >= RECORDS_TIMESTAMP_SHARE
        or delimited_share >= RECORDS_DELIMITED_SHARE
    ):
        content_class = RECORDS
    else:
        content_class = PROSE

    return ContentProfile(
        content_class=content_class,
        dump_object_headers=headers,
        sampled_lines=sampled,
        timestamp_share=round(timestamp_share, 3),
        delimited_share=round(delimited_share, 3),
    )


def merge_paragraphs(
    paragraphs: Iterable[str], max_chars: int
) -> Iterable[str]:
    """Join consecutive paragraphs with a blank line while they fit max_chars.

    A paragraph already at or over max_chars is yielded on its own, for the
    splitter to cut as before. Order and text are preserved.
    """
    buffer: List[str] = []
    size = 0
    for paragraph in paragraphs:
        if len(paragraph) >= max_chars:
            if buffer:
                yield "\n\n".join(buffer)
                buffer, size = [], 0
            yield paragraph
            continue
        added = len(paragraph) + (2 if buffer else 0)
        if buffer and size + added > max_chars:
            yield "\n\n".join(buffer)
            buffer, size, added = [], 0, len(paragraph)
        buffer.append(paragraph)
        size += added
    if buffer:
        yield "\n\n".join(buffer)


def split_dump_objects(text: str) -> Iterable[str]:
    """Yield a SQL dump as one block per object header.

    A block starts at the run of "--" comment lines holding a "-- Name:"
    header (the bare "--" above it and any "-- TOC entry" line) and runs to the
    next one. Whatever precedes the first header (SET statements, the dump
    banner) is a block of its own. Works on offsets so a large file is not
    copied into a list of lines.
    """
    starts = [0]
    for match in _DUMP_HEADER.finditer(text):
        start = match.start()
        while start > 0:
            prev_line = text.rfind("\n", 0, start - 1) + 1
            if not text.startswith("--", prev_line):
                break
            start = prev_line
        if start > starts[-1]:
            starts.append(start)
    starts.append(len(text))
    for begin, end in zip(starts, starts[1:]):
        block = text[begin:end].strip()
        if block:
            yield block
