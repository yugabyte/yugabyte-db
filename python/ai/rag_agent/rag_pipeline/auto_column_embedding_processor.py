"""
AutoColumnEmbeddingProcessor - generic "source table.column (text) ->
destination table.column (vector)" auto-embedding, registration-based.

Buffer-and-flush: rows the worker's poll loop claims via
``RegistrationPoller.claim_batch`` are handed to ``buffer()``, which appends
them and returns immediately. A separate flush (triggered from the same
poll loop, see ``maybe_flush``) embeds every buffered row's text in one
batched call per distinct embedding model once a volume-or-time trigger
fires, then finalizes each row. Unlike the earlier work_queue-based design,
claiming here already returns a whole batch per registration per poll cycle
(``RegistrationPoller.claim_batch``'s own ``destination_claim_batch_size``),
so accumulation doesn't depend on a single-row poll loop running fast --
this buffer just smooths out multiple registrations/poll cycles into fewer,
larger embed calls.

Real batching requires the OpenAI provider specifically: LangChain's
BedrockEmbeddings for Titan models loops ``invoke_model`` once per text
internally (Titan's raw API takes one ``inputText`` per call) -- wrapping
that in a "batch" buys zero network-call reduction. OpenAIEmbeddings.
embed_documents() genuinely sends many texts in one HTTP request.

Retry state (attempts/next_retry_at/last_error) lives entirely on the
``dist_rag.column_embedding_progress`` row (see RegistrationPoller.
finalize_retry), not in this processor's in-memory buffer -- a retryable
failure is finalized and then simply dropped from the buffer; the normal
claim query naturally re-discovers it once ``next_retry_at`` passes. This is
a real simplification over the earlier design, which needed an in-memory
ready/not-ready split because work_queue carried no persisted backoff field.
"""

import logging
import os
import threading
import time
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

from db.registration_cache import ColumnEmbeddingRegistration, RegistrationCache
from db.system_connection_pool import SystemConnectionPool
from db.source_connection_pool import SourceConnectionPool
from embeddings import EmbeddingsGenerator
from observability import meko_observe
from rag_pipeline.source_readers import SOURCE_READERS
from work_queue.registration_poller import ClaimedRow, RegistrationPoller

BATCH_MAX = int(os.getenv("COLUMN_EMBED_BATCH_MAX", "500"))
BATCH_MAX_WAIT_SECONDS = int(os.getenv("COLUMN_EMBED_BATCH_MAX_WAIT_SECONDS", "60"))
MAX_ATTEMPTS = int(os.getenv("COLUMN_EMBED_MAX_ATTEMPTS", "5"))
RETRY_BACKOFF_BASE_SECONDS = float(
    os.getenv("COLUMN_EMBED_RETRY_BACKOFF_BASE_SECONDS", "2")
)
PER_TENANT_FLUSH_CAP = int(os.getenv("COLUMN_EMBED_PER_TENANT_FLUSH_CAP", "100"))

# Fallback only -- normally every dist_rag.vector_indexes row already
# carries its own embedding_model_params.model, resolved per-registration by
# _get_embedder(). This constant is never used when that's the case.
FALLBACK_EMBEDDING_MODEL = os.getenv(
    "COLUMN_EMBED_FALLBACK_MODEL", "text-embedding-3-large"
)


@dataclass
class _BufferedItem:
    """A claimed row waiting to be flushed. No attempts/backoff state here
    -- that lives on the dist_rag.column_embedding_progress row instead
    (see module docstring), so a retryable failure is finalized and dropped
    from this buffer, not kept around waiting for its backoff to elapse."""

    claimed_row: ClaimedRow
    buffered_at: float = field(default_factory=time.time)


class AutoColumnEmbeddingProcessor:
    """
    Embeds a source table's text column(s) and writes the result to a
    destination table's vector column, for whatever registrations are
    currently ACTIVE -- driven entirely by ``dist_rag.column_embedding_registrations``
    data, not by any per-row task payload.
    """

    def __init__(
        self,
        on_terminal_failure=None,
        registration_cache: Optional[RegistrationCache] = None,
    ):
        """
        Args:
            on_terminal_failure: optional callback ``fn(claimed_row: ClaimedRow,
                error: str)`` invoked once a row is given up on for good
                (read miss or retries exhausted). Hook point for cross-repo
                concerns like refunding a caller's reserved quota unit --
                deliberately not implemented here, since that requires an
                endpoint in whatever repo inserted the row (e.g.
                meko-mcp-server's `_refund_free_tier_usage`), which is out
                of scope for this worker. Defaults to a log-only no-op.
            registration_cache: shared RegistrationCache instance. Accepting
                it here (rather than constructing a private one) lets the
                worker loop and this processor see the same cached
                registrations without a second, redundant DB round-trip.
        """
        self.logger = logging.getLogger(__name__)
        self.system_connection_pool = SystemConnectionPool()
        self.source_connection_pool = SourceConnectionPool()
        self.registration_cache = registration_cache or RegistrationCache()
        self.registration_poller = RegistrationPoller()
        self._on_terminal_failure = on_terminal_failure or self._log_terminal_failure
        self._buffer: List[_BufferedItem] = []
        self._lock = threading.Lock()
        self._embedders: Dict[str, EmbeddingsGenerator] = {}
        self._embedders_lock = threading.Lock()

    def _log_terminal_failure(self, claimed_row: ClaimedRow, error: str) -> None:
        registration = claimed_row.registration
        row_pk = claimed_row.row.get(registration.destination_pk_column)
        self.logger.error(
            f"Column embedding for {registration.registration_name!r} row "
            f"{row_pk} permanently failed: {error}"
        )

    def _get_embedder(self, registration: ColumnEmbeddingRegistration) -> EmbeddingsGenerator:
        """One EmbeddingsGenerator per distinct vector_index_id, cached --
        not rebuilt per row, and not read from a global env var (unlike the
        earlier design, since different registrations may reference
        different embedding models via their own vector_indexes row)."""
        key = str(registration.vector_index_id)
        with self._embedders_lock:
            embedder = self._embedders.get(key)
            if embedder is None:
                params = registration.embedding_model_params or {}
                embedder = EmbeddingsGenerator(
                    embedding_model=params.get("model", FALLBACK_EMBEDDING_MODEL),
                    embedding_model_params=params,
                    ai_provider=registration.ai_provider,
                )
                self._embedders[key] = embedder
            return embedder

    def _pools(self) -> Dict[str, Any]:
        return {
            "source": self.source_connection_pool,
            "system": self.system_connection_pool,
        }

    def buffer(self, claimed_rows: List[ClaimedRow]) -> None:
        """Append freshly claimed rows to the buffer and check whether it's
        time to flush. Does not finalize anything itself -- that happens
        inside ``_flush`` once the buffered batch is actually embedded and
        written."""
        if not claimed_rows:
            return
        with self._lock:
            self._buffer.extend(_BufferedItem(claimed_row=cr) for cr in claimed_rows)
            buffered_count = len(self._buffer)
        self.logger.info(
            f"Buffered {len(claimed_rows)} claimed row(s) "
            f"({buffered_count} pending in buffer)"
        )
        self.maybe_flush()

    def maybe_flush(self, force: bool = False) -> None:
        """
        Flush the buffer when either BATCH_MAX has accumulated or the
        oldest buffered row has waited BATCH_MAX_WAIT_SECONDS, whichever
        comes first. Call this from the poll loop's idle branch too (not
        just after buffer()) so a small batch that stops growing still gets
        flushed on the time trigger even when no new rows arrive.
        """
        with self._lock:
            if not self._buffer:
                return
            oldest_wait = time.time() - self._buffer[0].buffered_at
            should_flush = (
                force
                or len(self._buffer) >= BATCH_MAX
                or oldest_wait >= BATCH_MAX_WAIT_SECONDS
            )
            if not should_flush:
                return
            batch = self._buffer[:BATCH_MAX]
            self._buffer = self._buffer[BATCH_MAX:]

        self._flush(batch)

    @meko_observe(name="Flush Batch / AutoColumnEmbeddingProcessor", as_type="chain")
    def _flush(self, batch: List[_BufferedItem]) -> None:
        """Read each row's source text, embed everything grouped by
        embedding model in as few batched calls as possible, write results
        back, and finalize every row in the batch."""
        capped_batch = self._apply_per_tenant_cap(batch)

        groups: Dict[str, List[_BufferedItem]] = {}
        for item in capped_batch:
            key = str(item.claimed_row.registration.vector_index_id)
            groups.setdefault(key, []).append(item)

        for group_items in groups.values():
            self._flush_group(group_items)

    def _flush_group(self, group_items: List[_BufferedItem]) -> None:
        """Flush one vector_index_id group with a single embed_documents()
        call -- every item here shares the same embedding model."""
        registration = group_items[0].claimed_row.registration
        embedder = self._get_embedder(registration)

        read_ok: List[_BufferedItem] = []
        texts: List[str] = []
        for item in group_items:
            try:
                text = self._read_source_text(item.claimed_row)
            except Exception as e:
                self._handle_retryable_failure(item, f"source read failed: {e}")
                continue
            if not text:
                self._finalize_failure(item, "source text not found (permanent)")
                continue
            read_ok.append(item)
            texts.append(text)

        if not texts:
            return

        try:
            vectors = embedder.embedder.embed_documents(texts)
        except Exception as e:
            self.logger.error(f"Batch embed call failed for {len(texts)} texts: {e}")
            for item in read_ok:
                self._handle_retryable_failure(item, f"embed call failed: {e}")
            return

        if len(vectors) != len(read_ok):
            # Defensive: a provider that silently drops/reorders items would
            # otherwise write embeddings to the wrong destination rows.
            self.logger.error(
                f"Embedder returned {len(vectors)} vectors for "
                f"{len(read_ok)} texts; refusing to write, retrying batch"
            )
            for item in read_ok:
                self._handle_retryable_failure(item, "embed count mismatch")
            return

        for item, vector in zip(read_ok, vectors):
            try:
                self.registration_poller.finalize_success(item.claimed_row, vector)
            except Exception as e:
                self._handle_retryable_failure(item, f"destination write failed: {e}")

    def _apply_per_tenant_cap(self, batch: List[_BufferedItem]) -> List[_BufferedItem]:
        """Cap how many of one tenant's rows get processed in a single
        flush so a large backlog can't dominate a shared window; the
        remainder goes back to the buffer for the next round. Tenant key
        comes from ``registration.destination_tenant_column`` off the
        already-fetched claimed row -- optional per registration, no schema
        requirement. Absent a configured tenant column, every row from that
        registration shares one "unscoped" bucket and this is a no-op for
        it.
        """
        counts: Dict[str, int] = {}
        capped: List[_BufferedItem] = []
        deferred: List[_BufferedItem] = []
        for item in batch:
            registration = item.claimed_row.registration
            tenant_col = registration.destination_tenant_column
            tenant_key = (
                f"{registration.registration_name}:"
                f"{item.claimed_row.row.get(tenant_col, 'unscoped') if tenant_col else 'unscoped'}"
            )
            counts[tenant_key] = counts.get(tenant_key, 0)
            if counts[tenant_key] < PER_TENANT_FLUSH_CAP:
                counts[tenant_key] += 1
                capped.append(item)
            else:
                deferred.append(item)
        if deferred:
            with self._lock:
                self._buffer = deferred + self._buffer
        return capped

    def _read_source_text(self, claimed_row: ClaimedRow) -> Optional[str]:
        registration = claimed_row.registration
        source = {
            "connection": registration.source_connection,
            "schema": registration.source_schema,
            "table": registration.source_table,
            "text_columns": registration.source_text_columns,
            "filters": registration.source_filter_by_columns,
            "order_by": registration.source_order_by,
        }
        reader = SOURCE_READERS[source["connection"]]
        return reader(source, self._pools(), row=claimed_row.row)

    def _handle_retryable_failure(self, item: _BufferedItem, error: str) -> None:
        try:
            status = self.registration_poller.finalize_retry(
                item.claimed_row,
                error,
                max_attempts=MAX_ATTEMPTS,
                backoff_base_seconds=RETRY_BACKOFF_BASE_SECONDS,
            )
        except Exception as e:
            registration = item.claimed_row.registration
            row_pk = item.claimed_row.row.get(registration.destination_pk_column)
            self.logger.error(f"Failed to finalize_retry for row {row_pk}: {e}")
            return
        registration = item.claimed_row.registration
        row_pk = item.claimed_row.row.get(registration.destination_pk_column)
        if status == "FAILED":
            self.logger.warning(
                f"Row {row_pk} for {registration.registration_name!r} exhausted "
                f"retries ({error}); marked FAILED"
            )
            self._notify_terminal_failure(item.claimed_row, error)
        else:
            self.logger.warning(
                f"Row {row_pk} for {registration.registration_name!r} failed "
                f"({error}); re-queued for retry"
            )

    def _finalize_failure(self, item: _BufferedItem, error: str) -> None:
        try:
            self.registration_poller.finalize_failure(item.claimed_row, error)
        except Exception as e:
            self.logger.error(f"Failed to finalize_failure: {e}")
        self._notify_terminal_failure(item.claimed_row, error)

    def _notify_terminal_failure(self, claimed_row: ClaimedRow, error: str) -> None:
        try:
            self._on_terminal_failure(claimed_row, error)
        except Exception as e:
            self.logger.error(f"on_terminal_failure callback raised: {e}")
