from langchain_text_splitters import (
    CharacterTextSplitter,
    LatexTextSplitter,
    MarkdownTextSplitter,
    NLTKTextSplitter,
    PythonCodeTextSplitter,
    RecursiveCharacterTextSplitter,
    SpacyTextSplitter
)
from observability import meko_observe
import json
import mimetypes

SPLITTERS = {
    "character": CharacterTextSplitter,
    "latex": LatexTextSplitter,
    "markdown": MarkdownTextSplitter,
    "nltk": NLTKTextSplitter,
    "python": PythonCodeTextSplitter,
    "recursive_character": RecursiveCharacterTextSplitter,
    "spacy": SpacyTextSplitter,
}


MIME_TO_SPLITTER = {
    'text/markdown': 'markdown',
    'text/x-markdown': 'markdown',
    'text/x-python': 'python',
    'text/x-latex': 'latex',
    'application/x-latex': 'latex',
    'text/x-tex': 'latex',
    'text/csv': 'character',
}

DEFAULT_SPLITTER = 'recursive_character'

DEFAULT_SPLITTER_ARGS = {
    'character': '{"separator": "\\n", "chunk_size": 1000, "chunk_overlap": 100}',
    'markdown': '{"chunk_size": 1000, "chunk_overlap": 100}',
    'python': '{"chunk_size": 1500, "chunk_overlap": 200}',
    'latex': '{"chunk_size": 1000, "chunk_overlap": 100}',
}

DEFAULT_ARGS = '{"chunk_size": 1000, "chunk_overlap": 100}'


def get_splitter_for_filetype(file_location: str) -> tuple[str, str]:
    """
    Return (splitter_name, default_args_json) best suited for the given file's
    MIME type.  Falls back to recursive_character with generic defaults.
    """
    file_type, _ = mimetypes.guess_type(file_location)
    splitter = MIME_TO_SPLITTER.get(file_type, DEFAULT_SPLITTER)
    args = DEFAULT_SPLITTER_ARGS.get(splitter, DEFAULT_ARGS)
    return splitter, args


@meko_observe(name="Chunk Text / chunk", as_type="span")
def chunk(splitter, text, args):
    kwargs = json.loads(args)

    if splitter in SPLITTERS:
        return SPLITTERS[splitter](**kwargs).split_text(text)
    else:
        raise ValueError("Unknown splitter: {}".format(splitter))


@meko_observe(name="Chunk LangChain Docs / chunk", as_type="span")
def chunk_langchain_docs(splitter, docs, args):
    kwargs = json.loads(args)

    if splitter in SPLITTERS:
        return SPLITTERS[splitter](**kwargs).split_documents(docs)
    else:
        raise ValueError("Unknown splitter: {}".format(splitter))
