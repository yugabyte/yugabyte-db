FROM python:3.11.14-slim-trixie
RUN apt-get update && apt-get install -y --no-install-recommends \
        tesseract-ocr \
        libtesseract-dev \
        poppler-utils \
        libgl1 \
        libglib2.0-0 \
        libpq5 \
        libmagic1 \
    && rm -rf /var/lib/apt/lists/*
RUN python -c "import unstructured.partition.utils.xycut" 2>/dev/null || true
WORKDIR /app/rag_agent

COPY requirements.txt /app/rag_agent/
RUN pip install --no-cache-dir --upgrade pip && \
    pip install --no-cache-dir -r requirements.txt

# Pre-download Docling model artifacts at build time
ENV DOCLING_ARTIFACTS_PATH=/opt/docling-models
RUN python -c "from pathlib import Path; from docling.utils.model_downloader import download_models; download_models(output_dir=Path('/opt/docling-models'))" && \
    chmod -R a+rX /opt/docling-models

RUN useradd --uid 1001 --create-home --shell /bin/false ragagent

COPY --chown=ragagent:ragagent . /app/rag_agent/
RUN chown -R ragagent:ragagent /app/rag_agent

USER ragagent
RUN touch /app/rag_agent/app.log
ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONPATH=/app/rag_agent \
    NUMBA_CACHE_DIR=/tmp/numba_cache \
    MPLCONFIGDIR=/tmp/matplotlib
EXPOSE 8000
ENTRYPOINT ["python", "__main__.py"]
