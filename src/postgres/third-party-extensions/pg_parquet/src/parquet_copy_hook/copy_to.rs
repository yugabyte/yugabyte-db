use std::ffi::{c_char, CStr};

use pgrx::{
    ereport, is_a,
    pg_sys::{
        makeRangeVar, pg_plan_query, A_Star, ColumnRef, CommandTag, CopyStmt, CreateNewPortal,
        DestReceiver, GetActiveSnapshot, Node,
        NodeTag::{self, T_CopyStmt},
        ParamListInfoData, PlannedStmt, PortalDefineQuery, PortalDrop, PortalRun, PortalStart,
        QueryCompletion, QueryEnvironment, RawStmt, ResTarget, SelectStmt, CURSOR_OPT_PARALLEL_OK,
        RELKIND_FOREIGN_TABLE, RELKIND_MATVIEW, RELKIND_PARTITIONED_TABLE, RELKIND_RELATION,
        RELKIND_SEQUENCE, RELKIND_VIEW,
    },
    AllocatedByRust, PgBox, PgList, PgLogLevel, PgRelation, PgSqlErrorCode,
};

use crate::parquet_copy_hook::{
    copy_utils::{copy_stmt_has_relation, copy_stmt_lock_mode, copy_stmt_relation_oid},
    pg_compat::pg_analyze_and_rewrite,
};

// execute_copy_to_with_dest_receiver executes a COPY TO statement with our custom DestReceiver
// for writing to Parquet files.
// - converts the table relation to a SELECT statement if necessary
// - analyzes and rewrites the raw query
// - plans the rewritten query
// - creates a portal for the planned query by using the custom DestReceiver
// - executes the query with the portal
pub(crate) fn execute_copy_to_with_dest_receiver(
    p_stmt: &PgBox<PlannedStmt>,
    query_string: &CStr,
    params: &PgBox<ParamListInfoData>,
    query_env: &PgBox<QueryEnvironment>,
    parquet_dest: &PgBox<DestReceiver>,
) -> u64 {
    unsafe {
        debug_assert!(is_a(p_stmt.utilityStmt, T_CopyStmt));

        let copy_stmt = PgBox::<CopyStmt>::from_pg(p_stmt.utilityStmt as _);

        let mut relation = PgRelation::from_pg(std::ptr::null_mut());

        if copy_stmt_has_relation(p_stmt) {
            let rel_oid = copy_stmt_relation_oid(p_stmt);

            let lock_mode = copy_stmt_lock_mode(p_stmt);

            relation = PgRelation::with_lock(rel_oid, lock_mode);

            copy_to_stmt_ensure_table_kind(&relation);
        }

        let raw_query = prepare_copy_to_raw_stmt(p_stmt, &copy_stmt, &relation);

        let rewritten_queries = pg_analyze_and_rewrite(
            raw_query.as_ptr(),
            query_string.as_ptr(),
            query_env.as_ptr(),
        );

        let query = PgList::from_pg(rewritten_queries)
            .pop()
            .expect("rewritten query is empty");

        let plan = pg_plan_query(
            query,
            std::ptr::null(),
            CURSOR_OPT_PARALLEL_OK as _,
            params.as_ptr(),
        );

        let portal = CreateNewPortal();
        let mut portal = PgBox::from_pg(portal);
        portal.visible = false;

        let mut plans = PgList::<PlannedStmt>::new();
        plans.push(plan);

        PortalDefineQuery(
            portal.as_ptr(),
            std::ptr::null(),
            query_string.as_ptr(),
            CommandTag::CMDTAG_COPY,
            plans.as_ptr(),
            std::ptr::null_mut(),
        );

        PortalStart(portal.as_ptr(), params.as_ptr(), 0, GetActiveSnapshot());

        let mut completion_tag = QueryCompletion {
            commandTag: CommandTag::CMDTAG_COPY,
            nprocessed: 0,
        };

        PortalRun(
            portal.as_ptr(),
            i64::MAX,
            false,
            true,
            parquet_dest.as_ptr(),
            parquet_dest.as_ptr(),
            &mut completion_tag as _,
        );

        PortalDrop(portal.as_ptr(), false);

        completion_tag.nprocessed
    }
}

// prepare_copy_to_raw_stmt prepares a raw statement for the COPY TO operation.
// If the relation is not NULL, it converts the relation to a SELECT statement.
fn prepare_copy_to_raw_stmt(
    p_stmt: &PgBox<PlannedStmt>,
    copy_stmt: &PgBox<CopyStmt>,
    relation: &PgRelation,
) -> PgBox<RawStmt, AllocatedByRust> {
    let mut raw_query = unsafe { PgBox::<RawStmt>::alloc_node(NodeTag::T_RawStmt) };
    raw_query.stmt_location = p_stmt.stmt_location;
    raw_query.stmt_len = p_stmt.stmt_len;

    if relation.is_null() {
        raw_query.stmt = copy_stmt.query;
    } else {
        let select_stmt = convert_copy_to_relation_to_select_stmt(copy_stmt, relation);
        raw_query.stmt = select_stmt.into_pg() as _;
    }

    raw_query
}

fn convert_copy_to_relation_to_select_stmt(
    copy_stmt: &PgBox<CopyStmt>,
    relation: &PgRelation,
) -> PgBox<SelectStmt> {
    let mut target_list = PgList::new();

    if copy_stmt.attlist.is_null() {
        // SELECT * FROM relation
        let mut col_ref = unsafe { PgBox::<ColumnRef>::alloc_node(NodeTag::T_ColumnRef) };
        let a_star = unsafe { PgBox::<A_Star>::alloc_node(NodeTag::T_A_Star) };

        let mut field_list = PgList::new();
        field_list.push(a_star.into_pg());

        col_ref.fields = field_list.into_pg();
        col_ref.location = -1;

        let mut target = unsafe { PgBox::<ResTarget>::alloc_node(NodeTag::T_ResTarget) };
        target.name = std::ptr::null_mut();
        target.indirection = std::ptr::null_mut();
        target.val = col_ref.into_pg() as _;
        target.location = -1;

        target_list.push(target.into_pg());
    } else {
        // SELECT a,b,... FROM relation
        let attribute_name_list = unsafe { PgList::<Node>::from_pg(copy_stmt.attlist) };
        for attribute_name in attribute_name_list.iter_ptr() {
            let mut col_ref = unsafe { PgBox::<ColumnRef>::alloc_node(NodeTag::T_ColumnRef) };

            let mut field_list = PgList::new();
            field_list.push(attribute_name);

            col_ref.fields = field_list.into_pg();
            col_ref.location = -1;

            let mut target = unsafe { PgBox::<ResTarget>::alloc_node(NodeTag::T_ResTarget) };
            target.name = std::ptr::null_mut();
            target.indirection = std::ptr::null_mut();
            target.val = col_ref.into_pg() as _;
            target.location = -1;

            target_list.push(target.into_pg());
        }
    }

    let from = unsafe {
        makeRangeVar(
            relation.namespace().as_ptr() as _,
            relation.name().as_ptr() as _,
            -1,
        )
    };
    let mut from = unsafe { PgBox::from_pg(from) };
    from.inh = false;

    let mut select_stmt = unsafe { PgBox::<SelectStmt>::alloc_node(NodeTag::T_SelectStmt) };

    select_stmt.targetList = target_list.into_pg();

    let mut from_list = PgList::new();
    from_list.push(from.into_pg());
    select_stmt.fromClause = from_list.into_pg();

    select_stmt.into_pg_boxed()
}

// copy_to_stmt_ensure_table_kind ensures that the relation is a regular table.
// Taken from PG COPY TO code path.
fn copy_to_stmt_ensure_table_kind(relation: &PgRelation) {
    let relation_pgclass_entry = relation.rd_rel;
    let relation_kind = (unsafe { *relation_pgclass_entry }).relkind;

    if relation_kind == RELKIND_RELATION as c_char {
        return;
    }

    if relation_kind == RELKIND_VIEW as c_char {
        ereport!(
            PgLogLevel::ERROR,
            PgSqlErrorCode::ERRCODE_WRONG_OBJECT_TYPE,
            format!("cannot copy from view \"{}\"", relation.name()),
            "Try the COPY (SELECT ...) TO variant.",
        );
    } else if relation_kind == RELKIND_MATVIEW as c_char {
        ereport!(
            PgLogLevel::ERROR,
            PgSqlErrorCode::ERRCODE_WRONG_OBJECT_TYPE,
            format!("cannot copy from materialized view \"{}\"", relation.name()),
            "Try the COPY (SELECT ...) TO variant.",
        );
    } else if relation_kind == RELKIND_FOREIGN_TABLE as c_char {
        ereport!(
            PgLogLevel::ERROR,
            PgSqlErrorCode::ERRCODE_WRONG_OBJECT_TYPE,
            format!("cannot copy from foreign table \"{}\"", relation.name()),
            "Try the COPY (SELECT ...) TO variant.",
        );
    } else if relation_kind == RELKIND_SEQUENCE as c_char {
        ereport!(
            PgLogLevel::ERROR,
            PgSqlErrorCode::ERRCODE_WRONG_OBJECT_TYPE,
            format!("cannot copy from sequence \"{}\"", relation.name()),
            "Try the COPY (SELECT ...) TO variant.",
        );
    } else if relation_kind == RELKIND_PARTITIONED_TABLE as c_char {
        ereport!(
            PgLogLevel::ERROR,
            PgSqlErrorCode::ERRCODE_WRONG_OBJECT_TYPE,
            format!("cannot copy from partitioned table \"{}\"", relation.name()),
            "Try the COPY (SELECT ...) TO variant.",
        );
    } else {
        ereport!(
            PgLogLevel::ERROR,
            PgSqlErrorCode::ERRCODE_WRONG_OBJECT_TYPE,
            format!(
                "cannot copy from non-table relation \"{}\"",
                relation.name()
            ),
            "Try the COPY (SELECT ...) TO variant.",
        );
    }
}
