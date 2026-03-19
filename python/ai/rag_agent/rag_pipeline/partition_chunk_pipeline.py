from typing import Generator, Any, Dict
import os
import boto3
import logging
import requests
from rag_pipeline.chunk import chunk


def read_file_stream(
    file_location: str, chunk_size: int = 1024 * 1024
) -> Generator[str, None, None]:
    """
    Reads a file from S3 or local (NFS) as a stream of chunks.
    Assumes S3 paths are prefixed with 's3://', otherwise treats as local.
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
            while True:
                chunk = body.read(chunk_size)
                if not chunk:
                    break
                yield chunk.decode('utf-8')
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

                    for chunk_data in response.iter_content(
                        chunk_size=chunk_size, decode_unicode=True
                    ):
                        if chunk_data:
                            yield chunk_data

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


def stream_partition_and_chunk(
    pipeline_id: int,  # pipeline id of the document being processed
    file_location: str,
    chunk_args: Dict[str, Any] = {}
) -> Generator[str, None, None]:
    """
    Stream-partitions a text file from s3 or nfs into logical paragraphs,
    and further chunks paragraphs using chunk function from chunk.py.
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

    def line_iterator_from_chunks(text_chunks):
        buffer = ""
        for chunk_data in text_chunks:
            buffer += chunk_data
            while "\n" in buffer:
                line, buffer = buffer.split('\n', 1)
                yield line
        if buffer:
            yield buffer

    text_chunks = read_file_stream(file_location)
    lines_iter = line_iterator_from_chunks(text_chunks)
    logging.debug(f"generated lines_iter for file: {file_location}")
    for paragraph in paragraph_stream(lines_iter):
        # nikhil-todo: chunk args should be configured from the request chunk_embedding_kwargs.
        # Use default splitter and args if not provided
        splitter = chunk_args.get('splitter', 'recursive_character')
        args = chunk_args.get('args', '{"chunk_size": 1000, "chunk_overlap": 100}')
        logging.debug(f"chunking paragraph: with splitter: {splitter} and args: {args}")
        for chunked_text in chunk(splitter, paragraph, args):
            yield chunked_text
