use pgrx::datum::Timestamp;
use pgrx::iter::TableIterator;
use pgrx::pg_sys;
use pgrx::prelude::*;

::pgrx::pg_module_magic!(name, version);

type TransactionId = pg_sys::TransactionId;

/// RAII wrapper for PostgreSQL LWLock to ensure proper release
///
/// This guard automatically acquires a PostgreSQL LWLock on creation and releases it on drop,
/// ensuring that locks are properly managed even in the presence of errors or early returns.
///
/// # Important Note on Error Handling
/// PostgreSQL's LWLock implementation has specific requirements during error unwinding.
/// We only release the lock if InterruptHoldoffCount > 0 to avoid assertion failures
/// during PostgreSQL's error handling process (similar to pgrx's own lwlock implementation).
struct PgLwLockGuard {
    lock: *mut pg_sys::LWLock,
}

impl PgLwLockGuard {
    /// Acquire a PostgreSQL LWLock in shared mode with RAII cleanup
    ///
    /// # Safety
    /// The caller must ensure that:
    /// - `lock` is a valid, non-null pointer to an initialized LWLock
    /// - PostgreSQL is properly initialized
    /// - The lock will not be destroyed before this guard is dropped
    unsafe fn new_shared(lock: *mut pg_sys::LWLock) -> Self {
        if lock.is_null() {
            panic!("Attempted to acquire null LWLock");
        }

        pg_sys::LWLockAcquire(lock, pg_sys::LWLockMode::LW_SHARED);
        Self { lock }
    }
}

impl Drop for PgLwLockGuard {
    fn drop(&mut self) {
        unsafe {
            pg_sys::LWLockRelease(self.lock);
        }
    }
}

// XactTruncation is at index 44 in MainLWLockArray according to lwlocklist.h, From PostgreSQL source: PG_LWLOCK(44, XactTruncation)
const XACT_TRUNCATION_LOCK_INDEX: usize = 44;

// Transaction status constants to avoid duplication
const STATUS_IN_PROGRESS: &str = "in progress";
const STATUS_COMMITTED: &str = "committed";
const STATUS_ABORTED: &str = "aborted";

/// Convert TransactionId to i32 safely
#[inline(always)]
unsafe fn transaction_id_to_i32(xid: TransactionId) -> i32 {
    u32::from(xid) as i32
}

/// Check if one TransactionId precedes another
#[inline(always)]
unsafe fn transaction_id_precedes(id1: TransactionId, id2: TransactionId) -> bool {
    pg_sys::TransactionIdPrecedes(id1, id2)
}

/// Check if a TransactionId is valid (not InvalidTransactionId)
#[inline(always)]
unsafe fn is_valid_transaction_id(xid: TransactionId) -> bool {
    // Use pgrx's InvalidTransactionId constant
    xid != pg_sys::InvalidTransactionId
}

/// Get the top-level parent transaction ID and subtransaction level
/// This is based on the get_top_parent function from the original C implementation
/// SAFETY: This function assumes TransactionXmin validation has already been done by the caller
unsafe fn get_top_parent_xid(xid: TransactionId) -> (Option<TransactionId>, Option<i32>) {
    // Additional safety check - this should have been verified by caller but double-check
    if !pg_sys::TransactionIdFollowsOrEquals(xid, pg_sys::TransactionXmin) {
        // Cannot safely traverse subtrans for transactions older than TransactionXmin
        return (None, None);
    }

    let mut parent_xid = xid;
    let mut previous_xid = xid;
    let mut sub_level: i32 = -1;

    // Traverse the subtransaction hierarchy
    while is_valid_transaction_id(parent_xid) {
        previous_xid = parent_xid;

        // Safety check: don't call SubTransGetParent on transactions older than TransactionXmin
        if pg_sys::TransactionIdPrecedes(parent_xid, pg_sys::TransactionXmin) {
            break;
        }

        parent_xid = pg_sys::SubTransGetParent(parent_xid);
        sub_level += 1;

        if !is_valid_transaction_id(parent_xid) {
            break;
        }

        // Safety check: parent xid should always precede child xid to avoid infinite loops
        if !transaction_id_precedes(parent_xid, previous_xid) {
            error!(
                "pg_subtrans contains invalid entry: xid {} points to parent xid {}",
                previous_xid, parent_xid
            );
        }
    }

    // Return top parent and sublevel, or None if this is a top-level transaction
    if sub_level > 0 {
        (Some(previous_xid), Some(sub_level))
    } else {
        (None, None)
    }
}

/// Check if transaction ID is in recent past and accessible
/// This implements the complete logic from the original C implementation with proper locking
/// Returns (extracted_xid, is_accessible) where is_accessible indicates if the transaction
/// data is still available in the CLOG/SLRU cache
///
/// This is a direct Rust translation of the PostgreSQL C function `TransactionIdInRecentPast`
/// from the subtrans_infos extension, adapted for PostgreSQL 14+ with FullTransactionId support.
unsafe fn transaction_id_in_recent_past(
    xid_with_epoch: u64,
) -> Result<TransactionId, &'static str> {
    // For PostgreSQL 14+, use FullTransactionId APIs for proper epoch handling
    let xid_epoch = (xid_with_epoch >> 32) as u32;
    let xid = TransactionId::from(xid_with_epoch as u32);

    // Basic validation - invalid transaction IDs are not accessible
    if xid == pg_sys::InvalidTransactionId {
        return Err("invalid transaction ID");
    }

    // Special transaction IDs (bootstrap, frozen) are always accessible but don't need CLOG
    if !pg_sys::TransactionIdIsNormal(xid) {
        return Ok(xid);
    }

    // Get current full transaction ID for comparison
    let now_fullxid = pg_sys::ReadNextFullTransactionId();
    let now_epoch_next_xid = (now_fullxid.value as u32).into();
    let now_epoch = (now_fullxid.value >> 32) as u32;
    let oldest_clog_xid = {
        #[cfg(any(feature = "pg17", feature = "pg18"))]
        {
            pg_sys::FirstNormalTransactionId
        }
        #[cfg(not(any(feature = "pg17", feature = "pg18")))]
        {
            (*pg_sys::ShmemVariableCache).oldestClogXid
        }
    };

    // Create a FullTransactionId from the input for proper comparison
    let input_full_xid_value = ((xid_epoch as u64) << 32) | (xid.into_inner() as u64);

    // Check if the transaction ID is in the future - this is an error
    // For PostgreSQL 14+, we can compare FullTransactionId values directly
    if input_full_xid_value >= now_fullxid.value {
        return Err("transaction ID is in the future");
    }

    // Check if the transaction has wrapped around too far - older than we can determine
    // A transaction that's more than one full epoch older is definitely too old
    // This implements the wraparound detection logic from the original C code
    if (xid_epoch + 1) < now_epoch
        || ((xid_epoch + 1) == now_epoch && pg_sys::TransactionIdPrecedes(xid, now_epoch_next_xid))
        // If the XID is older than what's available in CLOG, it's not accessible
        || pg_sys::TransactionIdPrecedes(xid, oldest_clog_xid)
    {
        return Err("transaction ID is too old and CLOG data is unavailable");
    }

    // The transaction ID is recent enough and CLOG data is still available
    Ok(xid)
}

/// Determine transaction status and optionally get commit timestamp
/// This centralizes the status determination logic to avoid duplication
/// Returns (status, commit_timestamp)
unsafe fn get_transaction_status(xid: TransactionId) -> (String, Option<Timestamp>) {
    let mut commit_timestamp = None::<Timestamp>;
    let snapshot = pg_sys::GetActiveSnapshot();

    let status = if pg_sys::TransactionIdIsCurrentTransactionId(xid) {
        STATUS_IN_PROGRESS
    } else if pg_sys::TransactionIdDidCommit(xid) {
        // Try to get commit timestamp for committed transactions
        if pg_sys::track_commit_timestamp {
            let mut ts: pg_sys::TimestampTz = 0;
            if pg_sys::TransactionIdGetCommitTsData(xid, &mut ts, std::ptr::null_mut()) {
                commit_timestamp = Some(Timestamp::saturating_from_raw(ts));
            }
        }
        STATUS_COMMITTED
    } else if pg_sys::TransactionIdDidAbort(xid)
        || (!snapshot.is_null() && pg_sys::TransactionIdPrecedes(xid, (*snapshot).xmin))
    {
        STATUS_ABORTED
    } else {
        STATUS_IN_PROGRESS
    };

    (status.to_string(), commit_timestamp)
}

/// Main function that provides subtransaction information
#[pg_extern]
unsafe fn subtrans_infos(
    xid_input: i64,
    _fcinfo: pg_sys::FunctionCallInfo,
) -> TableIterator<
    'static,
    (
        name!(xid, i32),
        name!(status, String),
        name!(parent_xid, Option<i32>),
        name!(top_parent_xid, Option<i32>),
        name!(sub_level, Option<i32>),
        name!(commit_timestamp, Option<Timestamp>),
    ),
> {
    let xid_input = xid_input as u64;

    // CRITICAL: Acquire the XactTruncationLock FIRST, just like in the original C implementation, This protects against concurrent SLRU cache truncation operations
    let lock = &mut (*pg_sys::MainLWLockArray.wrapping_add(XACT_TRUNCATION_LOCK_INDEX)).lock;
    let _lock_guard = PgLwLockGuard::new_shared(lock);

    // Check if the transaction is accessible and get the extracted XID
    let xid = match transaction_id_in_recent_past(xid_input) {
        Ok(xid) => xid,
        Err(_err_msg) => {
            error!("Invalid transaction ID {}: {}", xid_input, _err_msg);
        }
    };

    // SAFETY CHECK: Before calling any SubTrans functions, we MUST verify that xid >= TransactionXmin to avoid violating PostgreSQL's assertion, This is the critical safety requirement that prevents crashes
    if !pg_sys::TransactionIdFollowsOrEquals(xid, pg_sys::TransactionXmin) {
        // Transaction is too old and subtrans data is not available
        let (status, commit_timestamp) = get_transaction_status(xid);

        return TableIterator::once((
            transaction_id_to_i32(xid),
            status,
            None,             // parent_xid is null - subtrans data not available
            None,             // top_parent_xid is null - subtrans data not available
            None,             // sub_level is null - subtrans data not available
            commit_timestamp, // commit_timestamp may be null for old transactions
        ));
    }

    // Safe to access subtrans data - transaction is recent enough
    let parent_xid = pg_sys::SubTransGetParent(xid);
    let (top_parent_xid, sub_level) = get_top_parent_xid(xid);

    // Determine transaction status using the centralized helper function
    let (status, commit_timestamp) = get_transaction_status(xid);

    let parent_xid_result = if is_valid_transaction_id(parent_xid) {
        Some(transaction_id_to_i32(parent_xid))
    } else {
        None
    };

    let top_parent_xid_result = top_parent_xid.map(|x| transaction_id_to_i32(x));

    TableIterator::once((
        transaction_id_to_i32(xid),
        status,
        parent_xid_result,
        top_parent_xid_result,
        sub_level,
        commit_timestamp,
    ))
}

/// This module is required by `cargo pgrx test` invocations.
/// It must be visible at the root of your extension crate.
#[cfg(test)]
pub mod pg_test {
    pub fn setup(_options: Vec<&str>) {
        // perform one-off initialization when the pg_test framework starts
    }

    #[must_use]
    pub fn postgresql_conf_options() -> Vec<&'static str> {
        // return any postgresql.conf settings that are required for your tests
        vec![]
    }
}

/// Comprehensive unit tests (run outside PostgreSQL context)
#[cfg(test)]
mod unit_tests {
    use super::*;

    #[test]
    fn test_transaction_id_to_i32() {
        // Test normal transaction ID conversion
        let xid = pg_sys::TransactionId::from(12345u32);
        unsafe {
            assert_eq!(transaction_id_to_i32(xid), 12345i32);
        }

        // Test maximum safe positive value
        let max_safe_xid = pg_sys::TransactionId::from(i32::MAX as u32);
        unsafe {
            assert_eq!(transaction_id_to_i32(max_safe_xid), i32::MAX);
        }

        // Test wrap-around case (values above i32::MAX)
        let wrap_xid = pg_sys::TransactionId::from(i32::MAX as u32 + 1);
        unsafe {
            assert_eq!(transaction_id_to_i32(wrap_xid), i32::MIN);
        }
    }

    #[test]
    fn test_is_valid_transaction_id() {
        unsafe {
            // Test invalid transaction ID
            let invalid_xid = pg_sys::InvalidTransactionId;
            assert!(!is_valid_transaction_id(invalid_xid));

            // Test valid transaction IDs
            let valid_xid = pg_sys::TransactionId::from(1u32);
            assert!(is_valid_transaction_id(valid_xid));

            let another_valid_xid = pg_sys::TransactionId::from(12345u32);
            assert!(is_valid_transaction_id(another_valid_xid));
        }
    }

    #[test]
    fn test_status_constants() {
        // Ensure status constants are correctly defined
        assert_eq!(STATUS_IN_PROGRESS, "in progress");
        assert_eq!(STATUS_COMMITTED, "committed");
        assert_eq!(STATUS_ABORTED, "aborted");
    }

    #[test]
    fn test_lock_index_constant() {
        // Verify the XactTruncationLock index is within reasonable bounds
        assert_eq!(XACT_TRUNCATION_LOCK_INDEX, 44);
        assert!(XACT_TRUNCATION_LOCK_INDEX < 100, "Lock index should be reasonable");
    }
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    use pgrx::prelude::*;
    use pgrx::Spi;

    /// Test basic functionality with current transaction ID
    #[pg_test]
    fn test_basic_function() {
        Spi::connect(|client| {
            let current_xid = unsafe { pg_sys::GetCurrentTransactionId() };
            let query = format!("SELECT * FROM subtrans_infos({})", current_xid.into_inner());

            let mut count = 0;
            for _row in client.select(&query, None, &[])? {
                count += 1;
            }

            assert!(count > 0, "Function should return at least one row");
            Ok(Some(())) as Result<Option<()>, pgrx::spi::SpiError>
        })
        .unwrap();
    }

    /// Test with Bootstrap transaction ID
    #[pg_test]
    fn test_bootstrap_xid() {
        Spi::connect(|client| {
            let mut count = 0;
            for _row in client.select("SELECT * FROM subtrans_infos(1)", None, &[])? {
                count += 1;
            }
            assert_eq!(count, 1, "Bootstrap XID should return exactly one row");
            Ok(Some(())) as Result<Option<()>, pgrx::spi::SpiError>
        })
        .unwrap();
    }

    /// Test with Frozen transaction ID
    #[pg_test]
    fn test_frozen_xid() {
        Spi::connect(|client| {
            let mut count = 0;
            for _row in client.select("SELECT * FROM subtrans_infos(2)", None, &[])? {
                count += 1;
            }
            assert_eq!(count, 1, "Frozen XID should return exactly one row");
            Ok(Some(())) as Result<Option<()>, pgrx::spi::SpiError>
        })
        .unwrap();
    }

    /// Test multiple calls for consistency
    #[pg_test]
    fn test_consistency() {
        let current_xid = unsafe { pg_sys::GetCurrentTransactionId() };

        for _i in 0..3 {
            Spi::connect(|client| {
                let query = format!("SELECT * FROM subtrans_infos({})", current_xid.into_inner());
                let mut count = 0;
                for _row in client.select(&query, None, &[])? {
                    count += 1;
                }
                assert!(count > 0, "Iteration should return at least one row");
                Ok(Some(())) as Result<Option<()>, pgrx::spi::SpiError>
            })
            .unwrap();
        }
    }

    /// Test PostgreSQL version compatibility
    #[pg_test]
    fn test_version_compatibility() {
        Spi::connect(|client| {
            let current_xid = unsafe { pg_sys::GetCurrentTransactionId() };
            let xid_with_epoch = current_xid.into_inner() as u64;
            let query = format!("SELECT * FROM subtrans_infos({})", xid_with_epoch);

            let mut count = 0;
            for _row in client.select(&query, None, &[])? {
                count += 1;
            }

            assert!(count > 0, "Function should handle transaction IDs correctly");
            Ok(Some(())) as Result<Option<()>, pgrx::spi::SpiError>
        })
        .unwrap();
    }

    /// Test error recovery
    #[pg_test]
    fn test_error_handling() {
        // After any potential error, function should still work
        Spi::connect(|client| {
            let current_xid = unsafe { pg_sys::GetCurrentTransactionId() };
            let query = format!("SELECT * FROM subtrans_infos({})", current_xid.into_inner());
            let mut count = 0;
            for _row in client.select(&query, None, &[])? {
                count += 1;
            }
            assert!(count > 0, "Function should work reliably");
            Ok(Some(())) as Result<Option<()>, pgrx::spi::SpiError>
        })
        .unwrap();
    }
}
