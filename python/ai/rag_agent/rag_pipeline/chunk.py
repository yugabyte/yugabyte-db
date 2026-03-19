from langchain_text_splitters import (
    CharacterTextSplitter,
    LatexTextSplitter,
    MarkdownTextSplitter,
    NLTKTextSplitter,
    PythonCodeTextSplitter,
    RecursiveCharacterTextSplitter,
    SpacyTextSplitter
)
import json

SPLITTERS = {
    "character": CharacterTextSplitter,
    "latex": LatexTextSplitter,
    "markdown": MarkdownTextSplitter,
    "nltk": NLTKTextSplitter,
    "python": PythonCodeTextSplitter,
    "recursive_character": RecursiveCharacterTextSplitter,
    "spacy": SpacyTextSplitter,
}


def chunk(splitter, text, args):
    kwargs = json.loads(args)

    if splitter in SPLITTERS:
        return SPLITTERS[splitter](**kwargs).split_text(text)
    else:
        raise ValueError("Unknown splitter: {}".format(splitter))


def chunk_langchain_docs(splitter, docs, args):
    kwargs = json.loads(args)

    if splitter in SPLITTERS:
        return SPLITTERS[splitter](**kwargs).split_documents(docs)
    else:
        raise ValueError("Unknown splitter: {}".format(splitter))
