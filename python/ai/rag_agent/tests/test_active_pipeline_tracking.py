# !/usr/bin/env python3
"""
Test suite for PipelineTracking's timing columns.

dist_rag.pipeline_details.last_started_at and completed_at are what an
ingest's duration is read from, so these pin which statements set them.
"""

import pytest
from unittest.mock import MagicMock, patch

from db.active_pipeline_tracking import PipelineTracking

PIPELINE_ID = "82c9bc6f-f454-476f-b0ef-f63a7bd71c22"


def _tracking():
    """PipelineTracking over a mocked pool; returns it and the cursor."""
    cursor = MagicMock()
    cursor.fetchone.return_value = (PIPELINE_ID,)
    conn = MagicMock()
    conn.cursor.return_value = cursor
    pool = MagicMock()
    pool.get_connection.return_value = conn
    with patch('db.active_pipeline_tracking.ConnectionPool', return_value=pool):
        tracking = PipelineTracking()
    return tracking, cursor


def _sql(cursor):
    return " ".join(cursor.execute.call_args[0][0].split())


class TestPipelineTrackingTimestamps:
    """Which statement sets which timestamp."""

    def test_insert_sets_last_started_at(self):
        tracking, cursor = _tracking()

        assert tracking.insert_pipeline_details(
            document_id="456", document_name="test_document.txt") == PIPELINE_ID

        sql = _sql(cursor)
        assert "last_started_at" in sql
        assert sql.endswith("VALUES (%s, %s, %s, %s, %s, %s, NOW()) RETURNING pipeline_id")

    def test_mark_pipeline_completed_sets_completed_at(self):
        tracking, cursor = _tracking()

        assert tracking.mark_pipeline_completed(PIPELINE_ID) is True

        assert "completed_at = NOW()" in _sql(cursor)
        assert cursor.execute.call_args[0][1] == ("COMPLETED", PIPELINE_ID)

    def test_update_pipeline_status_leaves_timestamps_alone(self):
        tracking, cursor = _tracking()

        tracking.update_pipeline_status(PIPELINE_ID, "FAILED")

        sql = _sql(cursor)
        assert "completed_at" not in sql
        assert "last_started_at" not in sql


if __name__ == "__main__":
    pytest.main([__file__])
