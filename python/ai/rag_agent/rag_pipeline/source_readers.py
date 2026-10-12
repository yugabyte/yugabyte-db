"""
Source readers for column-embedding registrations.

The registration-based auto-embedding framework (registration table,
discovery/claim, buffering, batching, destination write, retry, fairness) is
fully generic, including the connection pools themselves
(SourceConnectionPool/TargetConnectionPool have no source-specific knowledge
at all). This module makes the *read* step declarative too, rather than one
bespoke Python function per source kind: a registration's
``source_filter_by_columns`` lists plain-equality/JSONB-path filters plus,
when a filter's real value has to be resolved through an indirect lookup
first (e.g. Langfuse's conversation-embedding case: a datapack_id has to
become a langfuse_project_id via ``meko_system.langfuse_project_mapping``
before the actual observations query can run), a ``resolve`` sub-spec doing
that lookup generically too. Both of this repo's current sources --
Langfuse's ``clickhouse.observations`` and a plain same-database column --
turn out to be expressible with zero source-specific Python: they're the
same reader, ``read_generic_column``, driven entirely by data.

Every filter value -- and every resolver's own WHERE value -- can come from
one of two places: a literal baked into the registration
(``value``/``where_value``), or a column on the claimed destination row
itself (``target_value_from_column``/``where_value_from_row_column``). The
latter is what makes one registration's ``source_filter_by_columns`` apply
to every row it discovers, rather than describing a single specific row: a
conversation turn's ``trace_id`` filter value is *that row's own*
``conversation_id``, not a fixed literal decided at registration time.

``SOURCE_READERS`` stays a registry (not just one hardcoded function) as an
escape hatch for a hypothetical future source that genuinely can't be
expressed declaratively -- add a new key/function there without touching
the registration table or AutoColumnEmbeddingProcessor's control flow.
Today it only needs the one generic entry.

Filter entry shapes (a registration's ``source_filter_by_columns`` list,
ANDed together; each needs exactly one value source):
    # value from the claimed row:
    {"source_filter_on_column": "trace_id", "target_value_from_column": "conversation_id"}
    # literal value:
    {"source_filter_on_column": "trace_id", "value": "..."}
    # metadata->>'message_id' = row['message_id']:
    {"source_filter_on_column": "metadata", "json_key": "message_id",
     "target_value_from_column": "message_id"}
    # value comes from a lookup first, itself keyed by the row:
    {"source_filter_on_column": "project_id", "resolve": {
        "schema": "meko_system", "table": "langfuse_project_mapping",
        "select_column": "langfuse_project_id",
        "where_column": "datapack_id",
        "where_value_from_row_column": "tenant_id"}}

A reader has the shape
``reader(source: dict, pools: dict, row: Optional[dict] = None) -> Optional[str]``,
returning the assembled text ready for embedding (multiple text_columns are
joined with a blank line, matching how meko-mcp-server already assembles
question+answer for embedding), or None if the referenced row/observation --
or a resolver's lookup -- can't be found (a permanent failure, not a
transient one; callers should not retry a None result).
"""

import logging
from typing import Any, Dict, List, Optional, Tuple

from psycopg import sql

logger = logging.getLogger(__name__)


def _filter_value(f: Dict[str, Any], row: Optional[Dict[str, Any]]) -> Any:
    """Return a plain filter entry's value, from whichever single source it
    declares. Raises if the referenced row column is missing entirely --
    that's a registration/reader mismatch, not a data miss, and should
    surface loudly rather than as a quiet None result."""
    if "target_value_from_column" in f:
        column_name = f["target_value_from_column"]
        if row is None or column_name not in row:
            raise ValueError(
                f"filter for column {f.get('source_filter_on_column')!r} needs "
                f"row[{column_name!r}], but no such column was present on the claimed row"
            )
        return row[column_name]
    return f["value"]


def _resolve_filter_value(
    resolve_spec: Dict[str, Any], pools: Dict[str, Any], row: Optional[Dict[str, Any]]
) -> Optional[Any]:
    """Run one indirect-lookup resolver and return the resolved value, or
    None if the lookup found nothing. Fully generic: which pool, table and
    columns to use are all data from the resolver spec itself -- e.g. for
    Langfuse this is a datapack_id -> langfuse_project_id lookup against
    meko_system.langfuse_project_mapping (the same mapping
    DocumentPreprocessor._resolve_langfuse_client already relies on for the
    public/secret key pair), but nothing here knows that's what it's doing.
    """
    if "where_value_from_row_column" in resolve_spec:
        column_name = resolve_spec["where_value_from_row_column"]
        if row is None or column_name not in row:
            raise ValueError(
                f"resolver for {resolve_spec.get('select_column')!r} needs "
                f"row[{column_name!r}], but no such column was present on "
                f"the claimed row"
            )
        where_value = row[column_name]
    else:
        where_value = resolve_spec["where_value"]

    pool = pools[resolve_spec.get("pool", "system")]
    connection = None
    try:
        connection = pool.get_connection()
        cursor = connection.cursor()
        try:
            query = sql.SQL(
                "SELECT {select_column} FROM {schema}.{table} WHERE {where_column} = %s"
            ).format(
                select_column=sql.Identifier(resolve_spec["select_column"]),
                schema=sql.Identifier(resolve_spec["schema"]),
                table=sql.Identifier(resolve_spec["table"]),
                where_column=sql.Identifier(resolve_spec["where_column"]),
            )
            cursor.execute(query, (where_value,))
            row_result = cursor.fetchone()
        finally:
            cursor.close()
        if row_result:
            return row_result[0]
        logger.warning(
            f"Resolver lookup found no {resolve_spec['select_column']} for "
            f"{resolve_spec['where_column']}={where_value!r} "
            f"in {resolve_spec['schema']}.{resolve_spec['table']}"
        )
        return None
    except Exception as e:
        logger.error(f"Resolver lookup failed ({resolve_spec}): {str(e)}")
        raise
    finally:
        if connection is not None:
            pool.return_connection(connection)


def _build_filter_conditions(
    filters: List[Dict[str, Any]],
    pools: Dict[str, Any],
    row: Optional[Dict[str, Any]],
) -> Tuple[Optional[Any], Optional[list]]:
    """Resolve any indirect filter values and build a parameterized WHERE
    clause. Returns (None, None) if a resolver came back empty -- the
    caller should treat that as a permanent miss, same as a query that
    finds no matching row.
    """
    conditions = []
    params: List[Any] = []
    for f in filters:
        if "resolve" in f:
            value = _resolve_filter_value(f["resolve"], pools, row)
            if value is None:
                return None, None
        else:
            value = _filter_value(f, row)

        if "json_key" in f:
            conditions.append(
                sql.SQL("{column}->>{key} = %s").format(
                    column=sql.Identifier(f["source_filter_on_column"]),
                    key=sql.Literal(f["json_key"]),
                )
            )
        else:
            conditions.append(
                sql.SQL("{column} = %s").format(column=sql.Identifier(f["source_filter_on_column"]))
            )
        params.append(value)

    return sql.SQL(" AND ").join(conditions), params


def read_generic_column(
    source: Dict[str, Any],
    pools: Dict[str, Any],
    row: Optional[Dict[str, Any]] = None,
) -> Optional[str]:
    """
    Fetch text_columns from whatever row/observation a source's filters
    (after resolving any indirect ones) describe -- fully declarative, no
    source-specific Python. See module docstring for the filter shapes.

    Expects ``source['filters']`` (non-empty list) and
    ``source['text_columns']``; ``source['order_by']`` is optional
    (``{"column": ..., "direction": "ASC"|"DESC"}``, defaulting to no
    ordering -- needed for Langfuse's use case since a trace can carry more
    than one observation matching the same filters as data grows, and the
    oldest one is the turn actually being embedded).

    Args:
        source: the registration's source descriptor (schema/table/
            text_columns/filters/order_by -- the same shape regardless of
            whether it came from a ColumnEmbeddingRegistration or, in
            existing tests, a plain dict).
        pools: ``{"source": SourceConnectionPool-like, "system":
            SystemConnectionPool-like}`` -- readers needing an indirect
            resolver (like Langfuse's) use ``pools[resolve.pool]`` for that
            lookup; the main query always runs against ``pools["source"]``.
        row: the claimed destination row's full column data, needed when
            any filter entry uses ``target_value_from_column``/
            ``where_value_from_row_column`` to derive its value per-row
            rather than from a literal. None (the default) keeps
            literal-only filters working unchanged.

    Returns:
        The joined text_columns for the matching row, or None if no row
        matched, or a resolver filter found nothing (both permanent
        failures -- do not retry).
    """
    filters = source.get("filters")
    if not filters:
        raise ValueError("read_generic_column requires a non-empty source.filters")
    text_columns = source.get("text_columns")
    if not text_columns:
        raise ValueError("read_generic_column requires source.text_columns")

    conditions, params = _build_filter_conditions(filters, pools, row)
    if conditions is None:
        return None

    order_clause = sql.SQL("")
    order_by = source.get("order_by")
    if order_by:
        # order_by comes from the registration's r_source_order_by, which
        # create_column_embedding_mapping (PUBLIC-executable) accepts with
        # no validation of its own. sql.SQL() does no escaping, so the
        # direction must be checked against an allow-list before composing
        # it into the query -- otherwise a registration can inject
        # arbitrary SQL into the ORDER BY clause (e.g. a subquery, running
        # with this worker's DB privileges) even though the rest of the
        # query is parameterized.
        raw_direction = (order_by.get("direction") or "ASC").upper()
        if raw_direction not in ("ASC", "DESC"):
            raise ValueError(f"invalid order_by direction: {raw_direction!r}")
        direction = sql.SQL(raw_direction)
        order_clause = sql.SQL(" ORDER BY {column} {direction}").format(
            column=sql.Identifier(order_by["column"]), direction=direction
        )

    pool = pools["source"]
    connection = None
    try:
        connection = pool.get_connection()
        cursor = connection.cursor()
        try:
            query = sql.SQL(
                "SELECT {columns} FROM {schema}.{table} WHERE {conditions}{order} LIMIT 1"
            ).format(
                columns=sql.SQL(", ").join(sql.Identifier(c) for c in text_columns),
                schema=sql.Identifier(source["schema"]),
                table=sql.Identifier(source["table"]),
                conditions=conditions,
                order=order_clause,
            )
            cursor.execute(query, tuple(params))
            matched_row = cursor.fetchone()
        finally:
            cursor.close()
        connection.commit()
    except Exception as e:
        if connection:
            connection.rollback()
        logger.error(
            f"Failed to read {source.get('schema')}.{source.get('table')} "
            f"with filters {filters}: {str(e)}"
        )
        raise
    finally:
        if connection is not None:
            pool.return_connection(connection)

    if not matched_row:
        logger.warning(
            f"No row found in {source.get('schema')}.{source.get('table')} "
            f"for filters {filters}"
        )
        return None

    parts = [str(value) for value in matched_row if value]
    return "\n\n".join(parts)


SOURCE_READERS = {
    "generic": read_generic_column,
}
