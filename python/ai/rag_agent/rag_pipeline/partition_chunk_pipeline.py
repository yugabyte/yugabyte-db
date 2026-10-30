from typing import Generator, Any, Dict, Optional
import codecs
import os
import boto3
import logging
import requests
from rag_pipeline.chunk import chunk, get_splitter_for_filetype
from observability import meko_observe

# Guard against one get_object exhausting the worker's memory, since the whole
# object is read in one call. It is only that guard, not a product limit, so it
# has to stay above the largest per-file cap any caller enforces. Meko's
# api-server allows 60 MB per file on its top tier (ProTierMaxKBFileBytes in
# internal/service/free_tier.go; 5 MB free, 50 MB standard), and a source
# registered through dist_rag.create_source has no size check at all, because
# CreateSourceProcessorForAWS_S3 queues every object of a supported type. Keep
# this above 60 MB, or raise MAX_FILE_SIZE_BYTES per deployment.
DEFAULT_MAX_FILE_SIZE_BYTES = 64 * 1024 * 1024  # 64 MiB


# The Markdown fine-tuning path keeps its own, much lower cap. parse_markdown
# peaks at about 23.4x the input, measured linearly at 1, 5 and 16 MiB (24.6 MB,
# 122.8 MB and 392.9 MB), against roughly 2x for the plain-text path. At 64 MiB
# that is about 1.5 GiB for one task, times WORKER_CONCURRENCY, so the two paths
# cannot share one number. Raising this is MEKO-1128, which needs the worker
# memory sized for it first.
DEFAULT_MAX_MARKDOWN_FILE_SIZE_BYTES = 5 * 1024 * 1024  # 5 MiB


def _env_size_bytes(var_name: str, default: int) -> int:
    """Read a positive byte count from the environment, else `default`."""
    raw = os.getenv(var_name, "").strip()
    if not raw:
        return default
    try:
        value = int(raw)
    except ValueError:
        logging.warning(
            f"{var_name}={raw!r} is not an integer; using {default}"
        )
        return default
    if value <= 0:
        logging.warning(
            f"{var_name}={value} is not positive; using {default}"
        )
        return default
    return value


def max_file_size_bytes() -> int:
    """Size cap for a whole-file text read, overridable per deployment."""
    return _env_size_bytes(
        "MAX_FILE_SIZE_BYTES", DEFAULT_MAX_FILE_SIZE_BYTES
    )


def max_markdown_file_size_bytes() -> int:
    """Size cap for the Markdown fine-tuning read, overridable per deployment."""
    return _env_size_bytes(
        "MAX_MARKDOWN_FILE_SIZE_BYTES", DEFAULT_MAX_MARKDOWN_FILE_SIZE_BYTES
    )


def _iter_lines(text: str) -> Generator[str, None, None]:
    """Yield `text` split on "\\n", one line at a time.

    Same result as text.split("\\n"), without building the list: that retained
    an entry and a string per line until chunking finished, hundreds of MiB on
    a large file. Splits on "\\n" only - splitlines() and io.StringIO also break
    on \\r and U+2028, which would change chunk output.
    """
    start = 0
    while True:
        end = text.find("\n", start)
        if end == -1:
            yield text[start:]
            return
        yield text[start:end]
        start = end + 1


def _decode_utf8(byte_chunks) -> Generator[str, None, None]:
    """Decode an iterable of byte slices as one UTF-8 stream.

    A character split across two slices decodes correctly, and the final flush
    raises if the stream ends mid-character rather than dropping the bytes.
    """
    decoder = codecs.getincrementaldecoder("utf-8")()
    for data in byte_chunks:
        if not data:
            continue
        text = decoder.decode(data)
        if text:
            yield text
    tail = decoder.decode(b"", final=True)
    if tail:
        yield tail


def read_file_stream(
    file_location: str, chunk_size: int = 1024 * 1024
) -> Generator[str, None, None]:
    """
    Reads a file from S3 or local (NFS) as a stream of chunks.
    Assumes S3 paths are prefixed with 's3://', otherwise treats as local.

    Byte slices are decoded with an incremental decoder, so a multi-byte
    character that straddles a slice boundary does not raise
    UnicodeDecodeError.
    """
    if file_location.startswith("s3://"):
        logging.debug(f"Reading file from S3: {file_location}")
        try:
            # Create S3 client - works for public objects without credentials
            s3 = boto3.client('s3')
            path = file_location[5:]
            bucket, key = path.split('/', 1)
            obj = s3.get_object(Bucket=bucket, Key=key)
            body = obj['Body']
            yield from _decode_utf8(
                iter(lambda: body.read(chunk_size), b"")
            )
        except Exception as e:
            if ("NoCredentialsError" in str(type(e).__name__) or
                    "Unable to locate credentials" in str(e)):
                logging.warning(
                    f"S3 credentials not found, trying alternative method "
                    f"for public S3 object: {file_location}"
                )
                try:
                    # Alternative method: use requests to download from S3 public URL
                    path = file_location[5:]
                    bucket, key = path.split('/', 1)
                    public_url = f"https://{bucket}.s3.amazonaws.com/{key}"
                    logging.debug(f"Attempting to download from public URL: {public_url}")

                    response = requests.get(public_url, stream=True)
                    response.raise_for_status()

                    # iter_content(decode_unicode=True) decodes incrementally
                    # only when the response declares an encoding, and yields
                    # bytes otherwise. Decode here instead, as UTF-8, to match
                    # the boto3 branch.
                    yield from _decode_utf8(
                        response.iter_content(chunk_size=chunk_size)
                    )

                except requests.exceptions.RequestException as req_e:
                    logging.error(
                        f"Failed to download S3 file using public URL "
                        f"{public_url}: {req_e}"
                    )
                    raise RuntimeError(
                        f"S3 file {file_location} is not accessible via boto3 "
                        f"(no credentials) or as a public URL. Please ensure "
                        f"the S3 object has public read permissions or "
                        f"configure AWS credentials."
                    )
                except Exception as alt_e:
                    logging.error(
                        f"Unexpected error with alternative S3 download method: "
                        f"{alt_e}"
                    )
                    raise RuntimeError(
                        f"Failed to access S3 file {file_location} using both "
                        f"boto3 and public URL methods: {alt_e}"
                    )
            else:
                logging.error(f"Error accessing S3 file {file_location}: {e}")
                raise RuntimeError(f"Failed to access S3 file {file_location}: {e}")
    else:
        logging.debug(f"Reading file from local: {file_location}")
        with open(file_location, "r", encoding="utf-8") as f:
            while True:
                chunk = f.read(chunk_size)
                if not chunk:
                    break
                yield chunk

@meko_observe(name="Chunking & Partitioning", as_type="span")
def stream_partition_and_chunk(
    pipeline_id: int,  # pipeline id of the document being processed
    file_location: str,
    chunk_args: Dict[str, Any] = {}
) -> Generator[str, None, None]:
    """
    Partitions a text file from s3 or nfs into logical paragraphs, and
    further chunks paragraphs using chunk function from chunk.py.

    The file is read in one call before the first chunk is yielded. Reading it
    lazily held the S3 response open for the whole embedding run - minutes per
    megabyte - and the server dropped the idle connection mid-file.
    """
    def paragraph_stream(lines_iter):
        paragraph = []
        for line in lines_iter:
            if line.strip() == "":
                if paragraph:
                    yield "\n".join(paragraph).strip()
                    paragraph = []
            else:
                paragraph.append(line.rstrip('\n'))
        if paragraph:
            yield "\n".join(paragraph).strip()

    # nikhil-todo: chunk args should be configured from the request chunk_embedding_kwargs.
    auto_splitter, auto_args = get_splitter_for_filetype(file_location)
    splitter = chunk_args.get('splitter', auto_splitter)
    args = chunk_args.get('args', auto_args)
    logging.info(
        f"Using splitter '{splitter}' for file: {file_location} "
        f"(auto-detected: '{auto_splitter}')"
    )

    full_text = read_whole_file(
        file_location, max_size_bytes=max_file_size_bytes()
    )
    lines_iter = _iter_lines(full_text)
    logging.debug(f"generated lines_iter for file: {file_location}")
    for paragraph in paragraph_stream(lines_iter):
        for chunked_text in chunk(splitter, paragraph, args):
            yield chunked_text

def enforce_size_limit(
    file_location: str,
    size_bytes: Optional[int],
    max_size_bytes: Optional[int],
) -> None:
    if size_bytes is None or max_size_bytes is None:
        return
    if size_bytes > max_size_bytes:
        raise ValueError(
            f"File {file_location} is {size_bytes} bytes which exceeds "
            f"the {max_size_bytes}-byte "
            f"({max_size_bytes // (1024 * 1024)} MiB) size limit; "
            f"rejecting."
        )

def read_whole_file(
    file_location: str,
    max_size_bytes: Optional[int] = None,
) -> str:
    if file_location.startswith("s3://"):
        return _read_s3_whole(file_location, max_size_bytes)

    size_bytes = os.path.getsize(file_location)
    enforce_size_limit(file_location, size_bytes, max_size_bytes)
    logging.debug(
        f"Reading file from local: {file_location} ({size_bytes} bytes)"
    )
    with open(file_location, "r", encoding="utf-8") as f:
        return f.read()


def _read_s3_whole(
    file_location: str,
    max_size_bytes: Optional[int],
) -> str:
    path = file_location[5:]
    bucket, key = path.split("/", 1)

    logging.debug(f"Reading file from S3: {file_location}")
    try:
        s3 = boto3.client("s3")
        obj = s3.get_object(Bucket=bucket, Key=key)
        enforce_size_limit(
            file_location, obj.get("ContentLength"), max_size_bytes
        )
        return obj["Body"].read().decode("utf-8")
    except UnicodeDecodeError as e:
        # Before ValueError, which it subclasses: without this a decode failure
        # would propagate bare, without naming the file.
        logging.error(f"Error decoding S3 file {file_location}: {e}")
        raise RuntimeError(f"Failed to decode S3 file {file_location}: {e}")
    except ValueError:
        # enforce_size_limit's rejection: already names the file.
        raise
    except Exception as e:
        no_credentials = (
            "NoCredentialsError" in str(type(e).__name__)
            or "Unable to locate credentials" in str(e)
        )
        if not no_credentials:
            logging.error(f"Error accessing S3 file {file_location}: {e}")
            raise RuntimeError(
                f"Failed to access S3 file {file_location}: {e}"
            )

        public_url = f"https://{bucket}.s3.amazonaws.com/{key}"
        logging.warning(
            f"S3 credentials not found, falling back to public URL for "
            f"{file_location}: {public_url}"
        )
        try:
            with requests.get(public_url, stream=True) as response:
                response.raise_for_status()
                content_length_header = response.headers.get("Content-Length")
                if content_length_header is not None:
                    enforce_size_limit(
                        file_location,
                        int(content_length_header),
                        max_size_bytes,
                    )
                return response.content.decode("utf-8")
        except UnicodeDecodeError as e:
            logging.error(f"Error decoding S3 file {file_location}: {e}")
            raise RuntimeError(
                f"Failed to decode S3 file {file_location}: {e}"
            )
        except ValueError:
            raise
        except requests.exceptions.RequestException as req_e:
            logging.error(
                f"Failed to download S3 file using public URL "
                f"{public_url}: {req_e}"
            )
            raise RuntimeError(
                f"S3 file {file_location} is not accessible via boto3 "
                f"(no credentials) or as a public URL. Please ensure "
                f"the S3 object has public read permissions or "
                f"configure AWS credentials."
            )
        except Exception as alt_e:
            logging.error(
                f"Unexpected error with alternative S3 download method: "
                f"{alt_e}"
            )
            raise RuntimeError(
                f"Failed to access S3 file {file_location} using both "
                f"boto3 and public URL methods: {alt_e}"
            )
