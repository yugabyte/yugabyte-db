# RAG Preprocessor Tests

This directory contains comprehensive tests for the RAG preprocessor components.

## Test Structure

- `test_rag_handler.py` - Tests for the `RagPipelineHandler` class (`rag_pipeline.rag_handler`)
- `test_embed.py` - Tests for `EmbeddingsGenerator` (`embeddings.embed`)
- `test_chunk.py` - Tests for the `chunk` function and `SPLITTERS` dictionary (`rag_pipeline.chunk`)
- `test_pipeline_simple.py` - Simplified pipeline tests for `read_file_stream` and `stream_partition_and_chunk`
- `test_pipeline_final.py` - Extended pipeline tests including edge cases and performance
- `test_pipeline_comprehensive.py` - Full pipeline tests including S3 mocking
- `test_yugabytedb_vector_store.py` - Tests for the `YugabyteDBVectorStore` (`db.yugabytedb_vector_store`)
- `test_integration.py` - End-to-end integration tests across multiple components
- `conftest.py` - Shared test fixtures and configuration
- `pytest.ini` - Pytest configuration

## Running Tests

### Prerequisites

1. **Create and activate the virtual environment:**
   ```bash
   python3 -m venv venv
   source venv/bin/activate
   ```

2. **Install project and test dependencies:**
   ```bash
   pip install -r requirements.txt
   ```

### Running All Tests

From the project root directory:

```bash
# Using the shell script (recommended - handles venv and PYTHONPATH automatically)
./run_tests.sh

# Or manually with the venv activated
PYTHONPATH=. venv/bin/python -m pytest -c pytest.ini tests/

# With verbose output
./run_tests.sh -v

# With coverage report
./run_tests.sh --cov=. --cov-report=html
```

### Running Specific Tests

```bash
# Run only RagPipelineHandler tests
./run_tests.sh test_rag_handler.py

# Run only chunk tests
./run_tests.sh test_chunk.py

# Run tests with specific markers
./run_tests.sh -m unit

# Run a specific test method
./run_tests.sh test_rag_handler.py::TestRagPipelineHandler::test_init_valid_parameters

# Or with pytest directly (make sure venv is activated and PYTHONPATH is set)
PYTHONPATH=. pytest -c pytest.ini tests/test_rag_handler.py
```

### Test Categories

Tests are organized with the following markers (defined in `pytest.ini`):

- `unit` - Unit tests that don't require external dependencies
- `integration` - Integration tests that may require database connections
- `slow` - Tests that take longer to run
- `requires_db` - Tests that require a database connection
- `requires_openai` - Tests that require OpenAI API key

## Test Coverage

### `RagPipelineHandler` (`test_rag_handler.py`)

- **Initialization** - Default parameters, custom parameters, pipeline config
- **Document Ingestion** (`_ingest_document`) - Basic flow, missing metadata, missing chunk_kwargs, custom embedding model, missing model/dimensions validation
- **Error Handling** - Embedding generation failures, vector store insertion failures
- **Resource Management** - Connection cleanup

### `EmbeddingsGenerator` (`test_embed.py`)

- Basic embedding generation with temp files
- Custom chunk arguments and embedding models
- Empty files and whitespace-only content
- Unsupported file types
- Per-chunk error handling (continues on failure)
- Large files and unicode content
- Integration with different models

### Chunk (`test_chunk.py`)

- All splitter types: character, recursive_character, markdown, python, latex, nltk, spacy
- Edge cases: empty text, small/large chunk sizes, zero/large overlap, special characters, unicode
- `SPLITTERS` dictionary validation
- Mocked splitter behavior

### Pipeline (`test_pipeline_*.py`)

- `read_file_stream` - Local files (small, large, empty, nonexistent, unicode, various chunk sizes)
- `stream_partition_and_chunk` - Paragraph splitting, custom chunk args, edge cases, S3 files (mocked)
- Performance - Large file processing, memory efficiency, different chunk sizes

### `YugabyteDBVectorStore` (`test_yugabytedb_vector_store.py`)

- Initialization with default and custom schema
- Table existence checks
- Embedding insertion with various batch sizes
- Empty iterator, None metadata, complex metadata
- Table-not-exists error handling
- Index creation
- Connection cleanup and destructor

### Integration (`test_integration.py`)

- End-to-end document ingestion workflow
- Multiple document ingestion
- Different chunking strategies
- Error handling across the pipeline
- Large document processing
- Concurrent document processing simulation
- Chunk function integration with different splitters
- Embedding generation integration

## Mocking Strategy

Tests use comprehensive mocking to isolate components from external dependencies:

- **`ConnectionPool`** - Mocked at `db.yugabytedb_vector_store.ConnectionPool` to avoid database connections
- **`PipelineTracking`** - Mocked at `db.active_pipeline_tracking.PipelineTracking`
- **`YugabyteDBVectorStore`** - Mocked at `rag_pipeline.rag_handler.YugabyteDBVectorStore`
- **`EmbeddingsGenerator`** - Mocked at `rag_pipeline.rag_handler.EmbeddingsGenerator`
- **`OpenAIEmbeddings`** - Mocked at `embeddings.embed.OpenAIEmbeddings` to avoid real API calls
- **`PDFProcessor`** - Mocked at `embeddings.embed.PDFProcessor`
- **`boto3`** - Mocked at `rag_pipeline.partition_chunk_pipeline.boto3` for S3 tests
- **File I/O** - Uses `tempfile` for consistent test environments

## Fixtures

Shared fixtures are defined in `conftest.py`:

- `sample_connection_string` - Database connection string
- `sample_metadata` - Sample metadata dictionary
- `sample_chunk_kwargs` - Sample chunking parameters
- `sample_embedding_data` - Sample embedding vectors
- `sample_embedding_model_params` - Embedding model config (`model` + `dimensions`)
- `sample_document_uri` - Test document URI
- `sample_document_metadata_id` - Test document metadata ID
- `vector_dimension` - Standard vector dimension (768)
- `table_name` - Standard table name
- `temp_file` - Auto-cleaned temporary file
- `mock_vector_store` - Mocked `YugabyteDBVectorStore` instance
- `mock_openai_embeddings` - Mocked `OpenAIEmbeddings` instance
- `mock_connection_pool` - Mocked `ConnectionPool` with conn/cursor
- `mock_boto3_client` - Mocked S3 client
- `mock_chunk_function` - Mocked chunk function
- `setup_test_environment` (autouse) - Sets `OPENAI_API_KEY` env var

## Environment Variables

Tests automatically set up required environment variables via the `setup_test_environment` autouse fixture:

- `OPENAI_API_KEY` - Set to `test-api-key` to avoid real API calls

## Continuous Integration

The test suite is designed to run in CI environments without external dependencies. All tests use mocking to ensure they can run in isolated environments.

## Adding New Tests

When adding new tests:

1. Use the existing fixtures from `conftest.py`
2. Follow the naming convention: `test_<method_name>_<scenario>`
3. Add appropriate docstrings explaining what the test validates
4. Mock external dependencies at the correct module path (where the name is used, not where it's defined)
5. Add markers for test categorization if needed
6. For `insert_embeddings` tests, set `mock_cur.rowcount = 0` (or appropriate int) to avoid `TypeError` with arithmetic on Mock objects

## Troubleshooting

### Common Issues

1. **Import Errors** - Ensure `PYTHONPATH` includes the project root. The `run_tests.sh` script handles this automatically.
2. **Mock Issues** - Verify mocks target the correct module path. Patch where the name is *used*, not where it's *defined* (e.g. `rag_pipeline.rag_handler.YugabyteDBVectorStore`, not `db.yugabytedb_vector_store.YugabyteDBVectorStore`).
3. **Fixture Dependencies** - Check that all required fixtures are available in `conftest.py`.
4. **`TypeError: unsupported operand type(s) for +=: 'int' and 'Mock'`** - Set `mock_cur.rowcount` to an integer when testing `insert_embeddings`.

### Debug Mode

Run tests with additional debugging:

```bash
./run_tests.sh -v -s --tb=long
```

This will show print statements and detailed tracebacks for failed tests.
