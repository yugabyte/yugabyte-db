from __future__ import annotations

import logging
import os
import tempfile
import threading
from pathlib import Path
from typing import Callable, Optional, Tuple

import boto3

from observability import meko_observe

logger = logging.getLogger(__name__)
ARTIFACTS_PATH_ENV = "DOCLING_ARTIFACTS_PATH"
_converter = None
_converter_lock = threading.Lock()

def _resolve_artifacts_path() -> Optional[str]:
    raw = os.getenv(ARTIFACTS_PATH_ENV, "").strip()
    if not raw:
        logger.warning(
            "%s is not set; Docling may attempt to download model weights on "
            "first use. Set it to the pre-downloaded artifacts directory for "
            "offline, no-download-on-the-fly operation.",
            ARTIFACTS_PATH_ENV,
        )
        return None

    resolved = str(Path(raw).expanduser().resolve())
    if not os.path.isdir(resolved):
        logger.warning(
            "%s=%s does not point to an existing directory; Docling may try "
            "to download models on first use.",
            ARTIFACTS_PATH_ENV,
            resolved,
        )
    return resolved


def _build_converter():
    from docling.datamodel.base_models import InputFormat
    from docling.datamodel.pipeline_options import PdfPipelineOptions
    from docling.document_converter import DocumentConverter, PdfFormatOption

    artifacts_path = _resolve_artifacts_path()

    pipeline_options = PdfPipelineOptions(
        artifacts_path=artifacts_path,
        # Hard guarantee: never ship document content to any external service.
        enable_remote_services=False,
    )

    return DocumentConverter(
        format_options={
            InputFormat.PDF: PdfFormatOption(pipeline_options=pipeline_options),
        }
    )


def get_converter():
    global _converter
    if _converter is None:
        with _converter_lock:
            if _converter is None:
                logger.info("Initialising Docling DocumentConverter (offline)...")
                _converter = _build_converter()
                logger.info("Docling DocumentConverter ready")
    return _converter


def preload_docling_models() -> bool:
    try:
        from docling.datamodel.base_models import InputFormat

        converter = get_converter()
        converter.initialize_pipeline(InputFormat.PDF)
        logger.info("Docling PDF models preloaded successfully")
        return True
    except Exception as e:
        logger.error(
            "Failed to preload Docling models: %s.",
            str(e),
            exc_info=True,
        )
        return False


def _resolve_local_pdf_path(
    file_location: str,
) -> Tuple[str, Callable[[], None]]:
    if not file_location.startswith("s3://"):
        return file_location, lambda: None

    path = file_location[len("s3://"):]
    bucket, key = path.split("/", 1)
    logger.info("Downloading PDF from S3 for Docling: bucket=%s key=%s", bucket, key)

    s3 = boto3.client("s3")
    obj = s3.get_object(Bucket=bucket, Key=key)
    data = obj["Body"].read()

    fd, temp_path = tempfile.mkstemp(suffix=".pdf")
    with os.fdopen(fd, "wb") as fh:
        fh.write(data)

    def _cleanup() -> None:
        try:
            os.remove(temp_path)
        except OSError:
            logger.debug("Temp PDF already removed: %s", temp_path)

    return temp_path, _cleanup


@meko_observe(name="Convert PDF to Markdown (Docling)", as_type="span")
def convert_pdf_to_markdown(file_location: str) -> str:
    from langchain_docling.loader import DoclingLoader, ExportType

    local_path, cleanup = _resolve_local_pdf_path(file_location)
    try:
        loader = DoclingLoader(
            file_path=local_path,
            export_type=ExportType.MARKDOWN,
            converter=get_converter(),
        )
        docs = loader.load()
        markdown = "\n\n".join(
            d.page_content for d in docs if d.page_content
        ).strip()

        if not markdown:
            raise RuntimeError(
                f"Docling produced no Markdown content for {file_location}"
            )

        logger.info(
            "Converted PDF to Markdown via Docling: file=%s, markdown_chars=%d",
            file_location,
            len(markdown),
        )
        return markdown
    finally:
        cleanup()
