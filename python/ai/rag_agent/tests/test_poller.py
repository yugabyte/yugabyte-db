#!/usr/bin/env python3
"""
Tests for the work_queue.poller.Poller terminal-state helpers.

Covers two helpers used to finalize ``dist_rag.work_queue`` rows after a
task is done. Without these, rows lingered in IN_PROGRESS and the SQL-side
reaper re-queued them on lease expiry, causing already-completed documents
to be reprocessed.

- ``mark_completed`` DELETEs the row (the queue stays compact; authoritative
  completion state lives in dist_rag.documents / dist_rag.sources /
  dist_rag.pipeline_details).
- ``mark_failed`` UPDATEs the row to ``task_status='FAILED'`` and clears the
  lease so a reconciliation process can find failures by scanning
  ``dist_rag.work_queue``.
"""

import pytest
from unittest.mock import Mock, patch
from uuid import uuid4

from work_queue.poller import Poller


def _create_poller(mock_pool_cls):
    """Build a Poller wired to fully mocked DB primitives."""
    mock_pool = Mock()
    mock_pool_cls.return_value = mock_pool
    mock_conn = Mock()
    mock_cur = Mock()
    mock_pool.get_connection.return_value = mock_conn
    mock_conn.cursor.return_value = mock_cur
    poller = Poller()
    return poller, mock_pool, mock_conn, mock_cur


class TestPollerFinalize:
    """mark_completed DELETEs the row; mark_failed UPDATEs to FAILED and
    clears the lease. Both prevent the SQL-side reaper from re-queueing
    the task on lease expiry."""

    @patch('work_queue.poller.ConnectionPool')
    def test_mark_completed_deletes_row(self, mock_pool_cls):
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        task_id = uuid4()
        mock_cur.fetchone.return_value = (task_id,)

        assert poller.mark_completed(task_id) is True

        executed_sql, executed_params = mock_cur.execute.call_args[0]
        assert "DELETE FROM dist_rag.work_queue" in executed_sql
        assert "WHERE id = %s" in executed_sql
        assert executed_params == (str(task_id),)
        mock_conn.commit.assert_called_once()
        mock_pool.return_connection.assert_called_once_with(mock_conn)

    @patch('work_queue.poller.ConnectionPool')
    def test_mark_failed_updates_row(self, mock_pool_cls):
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        task_id = uuid4()
        mock_cur.fetchone.return_value = (task_id,)

        assert poller.mark_failed(task_id) is True

        executed_sql, executed_params = mock_cur.execute.call_args[0]
        assert "UPDATE dist_rag.work_queue" in executed_sql
        assert "task_status = 'FAILED'" in executed_sql
        assert "lease_token = NULL" in executed_sql
        assert "lease_expires_at = NULL" in executed_sql
        assert executed_params == (str(task_id),)
        mock_conn.commit.assert_called_once()
        mock_pool.return_connection.assert_called_once_with(mock_conn)

    @patch('work_queue.poller.ConnectionPool')
    def test_mark_completed_returns_false_when_row_missing(self, mock_pool_cls):
        """If the row was already deleted, fetchone is None. We must NOT
        raise -- the caller would mask the original outcome."""
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        mock_cur.fetchone.return_value = None

        assert poller.mark_completed(uuid4()) is False
        mock_conn.commit.assert_called_once()

    @patch('work_queue.poller.ConnectionPool')
    def test_mark_failed_rolls_back_and_raises_on_db_error(self, mock_pool_cls):
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        mock_cur.execute.side_effect = Exception("boom")

        with pytest.raises(Exception, match="boom"):
            poller.mark_failed(uuid4())

        mock_conn.rollback.assert_called_once()
        mock_pool.return_connection.assert_called_once_with(mock_conn)

    @patch('work_queue.poller.ConnectionPool')
    def test_mark_completed_accepts_string_id(self, mock_pool_cls):
        """work_queue ids round-trip through JSON and config as strings;
        the helper should coerce uniformly via str()."""
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        mock_cur.fetchone.return_value = ("11111111-1111-1111-1111-111111111111",)

        assert poller.mark_completed(
            "11111111-1111-1111-1111-111111111111"
        ) is True
        _, executed_params = mock_cur.execute.call_args[0]
        assert executed_params == ("11111111-1111-1111-1111-111111111111",)
