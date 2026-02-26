import os
import logging
import psycopg
from langchain_openai import OpenAIEmbeddings


class UserPromptEmbedder:
    """
    Class to embed user prompt using OpenAI and perform similarity search against a PostgreSQL table
    with HNSW vector index.
    """

    def __init__(self,
                 embedding_model: str = "text-embedding-ada-002",
                 openai_api_key: str = None,
                 pg_connection_string: str = None,
                 table_name: str = "pg_rag_default_store",
                 vector_column: str = "embeddings",
                 text_column: str = "chunk_text",
                 top_k: int = 5,
                 vector_dimension: int = 1536):
        self.embedding_model = embedding_model
        self.openai_api_key = openai_api_key or os.getenv("OPENAI_API_KEY")
        self.table_name = table_name
        self.vector_column = vector_column
        self.text_column = text_column
        self.top_k = top_k
        self.vector_dimension = vector_dimension
        self.pg_connection_string = (
            pg_connection_string or
            os.getenv("YUGABYTEDB_CONNECTION_STRING")
        )
        self.embedder = OpenAIEmbeddings(
            model=self.embedding_model,
            openai_api_key=self.openai_api_key,
        )
        self.logger = logging.getLogger(__name__)

    def embed_prompt(self, prompt: str):
        """Get OpenAI embedding for a single prompt."""
        try:
            embedding = self.embedder.embed_query(prompt)
            return embedding
        except Exception as e:
            self.logger.error(f"Failed to generate embedding for prompt: {e}")
            raise

    def similarity_search(self, prompt: str):
        """
        Embed the prompt and perform a vector similarity search using HNSW index in the PG table.
        Returns top_k (text, score) tuples.
        """
        embedding = self.embed_prompt(prompt)
        results = []
        conn = None
        try:
            conn = psycopg.connect(self.pg_connection_string)
            cur = conn.cursor()
            # Assuming 'vector_cosine_ops' distance. Change to 'vector_l2_ops' if needed.
            sql = f"""
                SELECT {self.text_column}, {self.vector_column} <=> %s::vector AS distance
                FROM {self.table_name}
                ORDER BY {self.vector_column} <=> %s::vector ASC
                LIMIT %s
            """
            self.logger.debug(f"Executing similarity search SQL: {sql}")
            cur.execute(sql, (embedding, embedding, self.top_k))
            for row in cur.fetchall():
                results.append({'text': row[0], 'distance': row[1]})
            cur.close()
        except Exception as e:
            self.logger.error(f"Error in similarity search: {e}")
            raise
        finally:
            if conn:
                conn.close()
        return results

# Example usage:
# embedder = UserPromptEmbedder()
# prompt = "What are the training drills for soccer?"
# results = embedder.similarity_search(prompt)
# for res in results:
#     print(f"Text: {res['text'][:80]}... | Similarity (distance): {res['distance']}")


if __name__ == "__main__":
    import argparse

    self.logger.info("Initializing embedder...")
    parser = argparse.ArgumentParser(
        description="Embed a user prompt and do similarity search."
    )
    parser.add_argument(
        "--prompt", type=str, required=True,
        help="The user prompt to embed and search."
    )
    parser.add_argument(
        "--top_k", type=int, default=5,
        help="Number of top results to return."
    )
    parser.add_argument(
        "--index_name", type=str, default="pg_rag_default_store",
        help="Name of the index to search."
    )

    args = parser.parse_args()

    embedder = UserPromptEmbedder(top_k=args.top_k, table_name=args.index_name)
    results = embedder.similarity_search(args.prompt)
    print(f"Top {args.top_k} results: {len(results)}")
    for i, res in enumerate(results, start=1):
        print(
            f"{i}. Text: {res['text'][:120]}... | "
            f"Similarity (distance): {res['distance']}"
        )


# python embedding_user_promt.py --prompt "Another thing I noticed was I felt
# exhausted during the match. My coach sent me a presentation on exerciese to
# build match fitness, can you suggest some exercise for building sprint
# stamina?" --top_k 10
