import logging
import boto3
import httpx
import tempfile
import os
from io import BytesIO
from typing import Generator, Dict, Any
from urllib.parse import urlparse
from unstructured.partition.html import partition_html
from langchain_core.documents import Document
from protego import Protego
from rag_pipeline.chunk import chunk_langchain_docs, DEFAULT_SPLITTER, DEFAULT_ARGS

_USER_AGENT = "YugabyteDB-RAG-Agent/1.0"
_DEFAULT_TIMEOUT = 30
_DEFAULT_HEADERS = {
    "User-Agent": _USER_AGENT
}


class HTMLProcessor:
    def __init__(self):
        pass

    def _load_html_from_local(self, file_path: str):
        logging.info(f"Loading HTML data from {file_path}")
        try:
            elements = partition_html(
                filename=file_path,
                chunking_strategy="by_title",
                max_characters=10000,
                new_after_n_chars=5000,
                combine_text_under_n_chars=2000,
            )
        except Exception as e:
            logging.error(
                f"Error processing HTML {file_path}: {e}", exc_info=True
            )
            raise

        logging.info(
            f"HTML data loaded: {len(elements)} elements extracted "
            f"from {file_path}"
        )
        return elements

    def _check_robots_txt(self, url: str):
        parsed = urlparse(url)
        robots_url = f"{parsed.scheme}://{parsed.netloc}/robots.txt"
        try:
            response = httpx.get(
                robots_url,
                timeout=_DEFAULT_TIMEOUT,
                headers=_DEFAULT_HEADERS,
                follow_redirects=True,
            )
            if response.status_code == 200:
                rp = Protego.parse(response.text)
                if not rp.can_fetch(url, _USER_AGENT):
                    raise RuntimeError(
                        f"URL {url} is disallowed by {robots_url}"
                    )
                logging.debug(
                    f"robots.txt at {robots_url} allows fetching {url}"
                )
            else:
                logging.debug(
                    f"robots.txt returned {response.status_code} at "
                    f"{robots_url}, proceeding"
                )
        except httpx.RequestError as e:
            logging.warning(
                f"Could not fetch {robots_url}: {e}. Proceeding anyway."
            )

    def _load_html_from_url(self, url: str):
        self._check_robots_txt(url)
        logging.info(f"Fetching HTML from URL: {url}")
        try:
            response = httpx.get(
                url,
                timeout=_DEFAULT_TIMEOUT,
                headers=_DEFAULT_HEADERS,
                follow_redirects=True,
            )
            response.raise_for_status()
        except httpx.HTTPStatusError as e:
            logging.error(f"HTTP {e.response.status_code} fetching {url}")
            raise RuntimeError(
                f"Failed to fetch HTML from {url}: "
                f"HTTP {e.response.status_code}"
            )
        except httpx.RequestError as e:
            logging.error(f"Request error fetching {url}: {e}")
            raise RuntimeError(f"Failed to fetch HTML from {url}: {e}")

        content_type = response.headers.get("content-type", "")
        if "html" not in content_type and "text" not in content_type:
            logging.warning(
                f"URL {url} returned Content-Type '{content_type}', "
                f"expected HTML. Proceeding anyway."
            )

        temp_file_path = ""
        with tempfile.NamedTemporaryFile(
            suffix='.html', delete=False
        ) as temp_file:
            temp_file.write(response.content)
            temp_file_path = temp_file.name

        try:
            elements = partition_html(
                filename=temp_file_path,
                chunking_strategy="by_title",
                max_characters=10000,
                new_after_n_chars=5000,
                combine_text_under_n_chars=2000,
            )
            logging.info(
                f"Successfully extracted {len(elements)} elements "
                f"from URL: {url}"
            )
            return elements
        finally:
            if os.path.exists(temp_file_path):
                os.remove(temp_file_path)

    def _load_html_from_s3(self, file_path: str):
        logging.debug(f"Reading HTML file from S3: {file_path}")
        try:
            s3 = boto3.client('s3')
            path = file_path[5:]
            bucket, key = path.split('/', 1)

            logging.info(f"Getting object from S3: {bucket}, {key}")
            s3_object = s3.get_object(Bucket=bucket, Key=key)
            s3_stream = s3_object['Body']
            html_bytes = BytesIO(s3_stream.read())
            s3_stream.close()

            temp_file_path = ""
            with tempfile.NamedTemporaryFile(
                suffix='.html', delete=False
            ) as temp_file:
                temp_file.write(html_bytes.getvalue())
                temp_file_path = temp_file.name
                html_bytes.close()
            logging.info(f"Creating temporary file: {temp_file_path}")

            try:
                elements = partition_html(
                    filename=temp_file_path,
                    chunking_strategy="by_title",
                    max_characters=10000,
                    new_after_n_chars=5000,
                    combine_text_under_n_chars=2000,
                )
                logging.info(
                    f"Successfully loaded {len(elements)} elements "
                    f"from S3 HTML"
                )
                return elements
            finally:
                if os.path.exists(temp_file_path):
                    os.remove(temp_file_path)

        except Exception as e:
            logging.error(f"Failed to read HTML file from S3: {e}")
            raise RuntimeError(
                f"Failed to read HTML file from S3: {e}"
            )

    def process_html_data(
        self, file_path: str, chunk_args: Dict[str, Any] = {}
    ) -> Generator[Document, None, None]:
        logging.debug(f"Processing HTML data from {file_path}")

        if file_path.startswith(("http://", "https://")):
            raw_elements = self._load_html_from_url(file_path)
        elif file_path.startswith("s3://"):
            raw_elements = self._load_html_from_s3(file_path)
        else:
            raw_elements = self._load_html_from_local(file_path)

        langchain_docs = []
        for el in raw_elements:
            metadata = el.metadata.to_dict()
            doc = Document(page_content=str(el), metadata=metadata)
            langchain_docs.append(doc)

        logging.info(
            f"LangChain documents created from HTML elements: "
            f"{len(langchain_docs)} documents created"
        )

        splitter = chunk_args.get('splitter', DEFAULT_SPLITTER)
        args = chunk_args.get('args', DEFAULT_ARGS)
        chunked_docs = chunk_langchain_docs(splitter, langchain_docs, args)
        for chunked_text in chunked_docs:
            yield chunked_text
