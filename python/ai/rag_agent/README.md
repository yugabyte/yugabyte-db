# YugabyteDB RAG Agent

A lightweight, high-performance Python RAG Agent built with FastAPI for RAG (Retrieval-Augmented Generation) text preprocessing operations alongside YugabyteDB.

## Features

- **Lightweight & Fast**: Built with FastAPI for maximum performance
- **Database Integration**: Seamless integration with YugabyteDB
- **Async Operations**: Full async/await support for high concurrency
- **Text Processing**: Built-in text preprocessing operations
- **Document Storage**: Store and retrieve processed documents
- **Search Capabilities**: Vector similarity search across processed documents

## Quick Start

### 1. Ubuntu Install Dependencies

Ubuntu System Dependencies
```
sudo apt-get update && sudo apt-get install -y tesseract-ocr libtesseract-dev leptonica-dev
sudo apt-get install -y \
  libgl1-mesa-glx \
  libglib2.0-0 \
  libsm6 \
  libxext6 \
  libxrender-dev \
  libgomp1
sudo apt-get install -y libpq-dev
sudo apt-get install -y poppler-utils
```

Python Dependencies

Pre-req: Install UV 

```bash
uv venv --python=3.11
source .venv/bin/activate
uv pip install -r requirements.txt
pip install -r requirements.txt
```

### 2. Configure Database (Optional)

Set environment variables for YugabyteDB connection:

```bash
export OPENAI_API_KEY=
export YUGABYTEDB_CONNECTION_STRING="postgresql://yugabyte:@127.0.0.1:5433/yugabyte"
export AWS_ACCESS_KEY_ID=
export AWS_SECRET_ACCESS_KEY=
export AWS_REGION=us-east-2
export AWS_S3_BUCKET_NAME=
```

### 3. Run the Server

```bash
python start_rag_agent.py
```

