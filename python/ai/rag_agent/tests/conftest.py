"""
Shared test fixtures and configuration for the RAG preprocessor tests.
"""
import pytest
import os
import tempfile
from unittest.mock import Mock, patch


@pytest.fixture
def sample_connection_string():
    """Sample database connection string for testing."""
    return "postgresql://user:password@localhost:5433/yugabyte"


@pytest.fixture
def sample_metadata():
    """Sample metadata dictionary for testing."""
    return {
        "source": "test_document",
        "type": "text",
        "version": "1.0",
        "author": "test_user"
    }


@pytest.fixture
def sample_chunk_kwargs():
    """Sample chunking arguments for testing."""
    return {
        "chunk_size": 1000,
        "chunk_overlap": 100,
        "splitter": "recursive_character"
    }


@pytest.fixture
def sample_embedding_data():
    """Sample embedding data for testing."""
    return [
        ("This is the first chunk of text.", [0.1, 0.2, 0.3, 0.4, 0.5]),
        ("This is the second chunk of text.", [0.6, 0.7, 0.8, 0.9, 1.0]),
        ("This is the third chunk of text.", [1.1, 1.2, 1.3, 1.4, 1.5])
    ]


@pytest.fixture
def temp_file():
    """Create a temporary file for testing file operations."""
    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
        f.write("This is a test document.\n\nIt has multiple paragraphs.\n\nFor testing purposes.")
        temp_file_path = f.name

    yield temp_file_path

    try:
        os.unlink(temp_file_path)
    except OSError:
        pass


@pytest.fixture
def mock_vector_store():
    """Create a mock YugabyteDBVectorStore for testing."""
    with patch('rag_pipeline.rag_handler.YugabyteDBVectorStore') as mock_store:
        mock_instance = Mock()
        mock_store.return_value = mock_instance
        yield mock_instance


@pytest.fixture
def mock_openai_embeddings():
    """Create a mock for OpenAI embeddings."""
    with patch('embeddings.embed.OpenAIEmbeddings') as mock_embeddings:
        mock_instance = Mock()
        mock_embeddings.return_value = mock_instance
        mock_instance.embed_documents.return_value = [[0.1, 0.2, 0.3]]
        yield mock_instance


@pytest.fixture
def mock_connection_pool():
    """Create a mock for the ConnectionPool singleton."""
    with patch('db.yugabytedb_vector_store.ConnectionPool') as mock_pool_class:
        mock_pool = Mock()
        mock_pool_class.return_value = mock_pool
        mock_conn = Mock()
        mock_cur = Mock()
        mock_pool.get_connection.return_value = mock_conn
        mock_conn.cursor.return_value = mock_cur
        yield mock_pool, mock_conn, mock_cur


@pytest.fixture
def mock_boto3_client():
    """Create a mock for boto3 S3 client."""
    with patch('rag_pipeline.partition_chunk_pipeline.boto3.client') as mock_client:
        mock_s3 = Mock()
        mock_client.return_value = mock_s3

        mock_obj = {'Body': Mock()}
        mock_s3.get_object.return_value = mock_obj

        mock_body = Mock()
        mock_obj['Body'] = mock_body
        mock_body.read.side_effect = [b"Test S3 content", b""]

        yield mock_s3


@pytest.fixture
def mock_chunk_function():
    """Create a mock for the chunk function."""
    with patch('rag_pipeline.partition_chunk_pipeline.chunk') as mock_chunk:
        mock_chunk.return_value = ["chunk1", "chunk2", "chunk3"]
        yield mock_chunk


@pytest.fixture(autouse=True)
def setup_test_environment():
    """Setup test environment variables."""
    os.environ['OPENAI_API_KEY'] = 'test-api-key'
    yield


@pytest.fixture
def sample_document_uri():
    """Sample document URI for testing."""
    return "test_document.txt"


@pytest.fixture
def sample_document_metadata_id():
    """Sample document metadata ID for testing."""
    return 12345


@pytest.fixture
def vector_dimension():
    """Standard vector dimension for testing."""
    return 768


@pytest.fixture
def table_name():
    """Standard table name for testing."""
    return "test_rag_store"


@pytest.fixture
def sample_embedding_model_params():
    """Sample embedding model parameters for testing."""
    return {
        "model": "text-embedding-ada-002",
        "dimensions": 768
    }
