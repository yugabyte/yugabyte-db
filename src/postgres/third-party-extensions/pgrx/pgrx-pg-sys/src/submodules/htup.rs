//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::{
    bits8, getmissingattr, heap_getsysattr, nocachegetattr, CommandId, Datum,
    FormData_pg_attribute, FrozenTransactionId, HeapTupleData, HeapTupleHeaderData, TransactionId,
    TupleDesc, HEAP_HASNULL, HEAP_HOT_UPDATED, HEAP_NATTS_MASK, HEAP_ONLY_TUPLE, HEAP_XMAX_INVALID,
    HEAP_XMIN_COMMITTED, HEAP_XMIN_FROZEN, HEAP_XMIN_INVALID, SIZEOF_DATUM,
};

/// # Safety
///
/// Caller must ensure `tup` is a valid [`HeapTupleHeaderData`] pointer
#[inline(always)]
pub unsafe fn HeapTupleHeaderIsHeapOnly(tup: *const HeapTupleHeaderData) -> bool {
    // #define HeapTupleHeaderIsHeapOnly(tup) \
    //    ( \
    //       ((tup)->t_infomask2 & HEAP_ONLY_TUPLE) != 0 \
    //    )

    unsafe {
        // SAFETY:  caller has asserted `htup_header` is a valid HeapTupleHeaderData pointer
        ((*tup).t_infomask2 & HEAP_ONLY_TUPLE as u16) != 0
    }
}

/// # Safety
///
/// Caller must ensure `tup` is a valid [`HeapTupleHeaderData`] pointer
#[inline(always)]
pub unsafe fn HeapTupleHeaderIsHotUpdated(tup: *const HeapTupleHeaderData) -> bool {
    // #define HeapTupleHeaderIsHotUpdated(tup) \
    // ( \
    //      ((tup)->t_infomask2 & HEAP_HOT_UPDATED) != 0 && \
    //      ((tup)->t_infomask & HEAP_XMAX_INVALID) == 0 && \
    //      !HeapTupleHeaderXminInvalid(tup) \
    // )

    unsafe {
        // SAFETY:  caller has asserted `htup_header` is a valid HeapTupleHeaderData pointer
        (*tup).t_infomask2 & HEAP_HOT_UPDATED as u16 != 0
            && (*tup).t_infomask & HEAP_XMAX_INVALID as u16 == 0
            && !HeapTupleHeaderXminInvalid(tup)
    }
}

/// # Safety
///
/// Caller must ensure `tup` is a valid [`HeapTupleHeaderData`] pointer
#[inline(always)]
pub unsafe fn HeapTupleHeaderXminInvalid(tup: *const HeapTupleHeaderData) -> bool {
    // #define HeapTupleHeaderXminInvalid(tup) \
    // ( \
    //   ((tup)->t_infomask & (HEAP_XMIN_COMMITTED|HEAP_XMIN_INVALID)) == \
    //      HEAP_XMIN_INVALID \
    // )

    unsafe {
        // SAFETY:  caller has asserted `htup_header` is a valid HeapTupleHeaderData pointer
        (*tup).t_infomask & (HEAP_XMIN_COMMITTED as u16 | HEAP_XMIN_INVALID as u16)
            == HEAP_XMIN_INVALID as u16
    }
}

/// Does the specified [`HeapTupleHeaderData`] represent a "frozen" tuple?
///
/// # Safety
///
/// Caller must ensure `tup` is a valid [`HeapTupleHeaderData`] pointer
#[inline(always)]
pub unsafe fn HeapTupleHeaderFrozen(tup: *const HeapTupleHeaderData) -> bool {
    // #define HeapTupleHeaderXminFrozen(tup) \
    // ( \
    // 	((tup)->t_infomask & (HEAP_XMIN_FROZEN)) == HEAP_XMIN_FROZEN \
    // )

    unsafe {
        // SAFETY:  caller has asserted `tup` is a valid HeapTupleHeader pointer
        (*tup).t_infomask & (HEAP_XMIN_FROZEN as u16) == (HEAP_XMIN_FROZEN as u16)
    }
}

/// HeapTupleHeaderGetRawCommandId will give you what's in the header whether
/// it is useful or not.  Most code should use HeapTupleHeaderGetCmin or
/// HeapTupleHeaderGetCmax instead, but note that those Assert that you can
/// get a legitimate result, ie you are in the originating transaction!
///
/// # Safety
///
/// Caller must ensure `tup` is a valid [`HeapTupleHeaderData`] pointer
#[inline(always)]
pub unsafe fn HeapTupleGetRawCommandId(tup: *const HeapTupleHeaderData) -> CommandId {
    // #define HeapTupleHeaderGetRawCommandId(tup) \
    // ( \
    // 	(tup)->t_choice.t_heap.t_field3.t_cid \
    // )

    unsafe {
        // SAFETY:  caller has asserted `tup` is a valid HeapTupleHeader pointer
        (*tup).t_choice.t_heap.t_field3.t_cid
    }
}

/// HeapTupleHeaderGetRawXmin returns the "raw" xmin field, which is the xid
/// originally used to insert the tuple.  However, the tuple might actually
/// be frozen (via HeapTupleHeaderSetXminFrozen) in which case the tuple's xmin
/// is visible to every snapshot.  Prior to PostgreSQL 9.4, we actually changed
/// the xmin to FrozenTransactionId, and that value may still be encountered
/// on disk.
///
/// # Safety
///
/// Caller must ensure `tup` is a valid [`HeapTupleHeaderData`] pointer
#[inline(always)]
pub unsafe fn HeapTupleHeaderGetRawXmin(tup: *const HeapTupleHeaderData) -> TransactionId {
    // #define HeapTupleHeaderGetRawXmin(tup) \
    // ( \
    // 	(tup)->t_choice.t_heap.t_xmin \
    // )
    unsafe {
        // SAFETY:  caller has asserted `tup` is a valid HeapTupleHeader pointer
        (*tup).t_choice.t_heap.t_xmin
    }
}

/// Returns the `xmin` value of the specified [`HeapTupleHeaderData`]
///
/// # Safety
///
/// Caller must ensure `tup` is a valid [`HeapTupleHeaderData`] pointer
#[inline(always)]
pub unsafe fn HeapTupleHeaderGetXmin(tup: *const HeapTupleHeaderData) -> TransactionId {
    // #define HeapTupleHeaderGetXmin(tup) \
    // ( \
    // 	HeapTupleHeaderXminFrozen(tup) ? \
    // 		FrozenTransactionId : HeapTupleHeaderGetRawXmin(tup) \
    // )

    unsafe {
        // SAFETY:  caller has asserted `tup` is a valid HeapTupleHeader pointer
        if HeapTupleHeaderFrozen(tup) {
            FrozenTransactionId
        } else {
            HeapTupleHeaderGetRawXmin(tup)
        }
    }
}

/// How many attributes does the specified [`HeapTupleHeader`][crate::HeapTupleHeader] have?
///
/// # Safety
///
/// Caller is responsible for ensuring `tup` is a valid pointer
#[inline(always)]
pub unsafe fn HeapTupleHeaderGetNatts(tup: *const HeapTupleHeaderData) -> u16 {
    // #define HeapTupleHeaderGetNatts(tup) \
    // 	((tup)->t_infomask2 & HEAP_NATTS_MASK)
    unsafe {
        // SAFETY:  caller has asserted that `tup` is a valid, non-null, pointer to a HeapTupleHeaderData struct
        (*tup).t_infomask2 & (HEAP_NATTS_MASK as u16)
    }
}

/// Does the specified [`HeapTuple`][crate::HeapTuple] contain nulls?
///
/// # Safety
///
/// Caller is responsible for ensuring `tup` is a valid pointer
#[inline(always)]
pub unsafe fn HeapTupleNoNulls(tup: *const HeapTupleData) -> bool {
    // #define HeapTupleNoNulls(tuple) \
    // 		(!((tuple)->t_data->t_infomask & HEAP_HASNULL))

    unsafe {
        // SAFETY:  caller has asserted that 'tup' is a valid, non-null pointer to a HeapTuple struct
        (*(*tup).t_data).t_infomask & (HEAP_HASNULL as u16) == 0
    }
}

/// # Safety
///
/// Caller is responsible for ensuring `BITS` is a valid [`bits8`] pointer of the right length to
/// accommodate `ATT >> 3`
#[inline(always)]
unsafe fn att_isnull(ATT: i32, BITS: *const bits8) -> bool {
    //    #define att_isnull(ATT, BITS) (!((BITS)[(ATT) >> 3] & (1 << ((ATT) & 0x07))))
    let ATT = ATT as usize;
    let slot = BITS.add(ATT >> 3);
    (*slot & (1 << (ATT & 0x07))) == 0
}

/// # Safety
///
/// Caller is responsible for ensuring `A` is a valid [`FormData_pg_attribute`] pointer
#[inline(always)]
unsafe fn fetchatt(A: *const FormData_pg_attribute, T: *mut std::os::raw::c_char) -> Datum {
    // #define fetchatt(A,T) fetch_att(T, (A)->attbyval, (A)->attlen)

    unsafe {
        // SAFETY:  caller has asserted `A` is a valid FromData_pg_attribute pointer
        fetch_att(T, (*A).attbyval, (*A).attlen)
    }
}

/// Given a Form_pg_attribute and a pointer into a tuple's data area,
/// return the correct value or pointer.
///
/// We return a Datum value in all cases.  If the attribute has "byval" false,
/// we return the same pointer into the tuple data area that we're passed.
/// Otherwise, we return the correct number of bytes fetched from the data
/// area and extended to Datum form.
///
/// On machines where Datum is 8 bytes, we support fetching 8-byte byval
/// attributes; otherwise, only 1, 2, and 4-byte values are supported.
///
/// # Safety
///
/// Note that T must be non-null and already properly aligned for this to work correctly.
#[inline(always)]
unsafe fn fetch_att(T: *mut std::os::raw::c_char, attbyval: bool, attlen: i16) -> Datum {
    unsafe {
        // #define fetch_att(T,attbyval,attlen) \
        // ( \
        // 	(attbyval) ? \
        // 	( \
        // 		(attlen) == (int) sizeof(Datum) ? \
        // 			*((Datum *)(T)) \
        // 		: \
        // 	  ( \
        // 		(attlen) == (int) sizeof(int32) ? \
        // 			Int32GetDatum(*((int32 *)(T))) \
        // 		: \
        // 		( \
        // 			(attlen) == (int) sizeof(int16) ? \
        // 				Int16GetDatum(*((int16 *)(T))) \
        // 			: \
        // 			( \
        // 				AssertMacro((attlen) == 1), \
        // 				CharGetDatum(*((char *)(T))) \
        // 			) \
        // 		) \
        // 	  ) \
        // 	) \
        // 	: \
        // 	PointerGetDatum((char *) (T)) \
        // )

        // SAFETY:  The only "unsafe" below is dereferencing T, and the caller has assured us it's non-null
        if attbyval {
            let attlen = attlen as usize;

            // NB:  Compiler should solve this branch for us, and we write it like this to avoid
            // code duplication for the case where a Datum isn't 8 bytes wide
            if SIZEOF_DATUM == 8 && attlen == std::mem::size_of::<Datum>() {
                return *T.cast::<Datum>();
            }

            if attlen == std::mem::size_of::<i32>() {
                Datum::from(*T.cast::<i32>())
            } else if attlen == std::mem::size_of::<i16>() {
                Datum::from(*T.cast::<i16>())
            } else {
                assert_eq!(attlen, 1);
                Datum::from(*T.cast::<std::os::raw::c_char>())
            }
        } else {
            Datum::from(T.cast::<std::os::raw::c_char>())
        }
    }
}

/// Extract an attribute of a heap tuple and return it as a Datum.
/// This works for either system or user attributes.  The given attnum
/// is properly range-checked.
///
/// If the field in question has a NULL value, we return a zero [`Datum`]
/// and set `*isnull == true`.  Otherwise, we set `*isnull == false`.
///
/// # Safety
///
/// - `tup` is the pointer to the heap tuple.
/// - `attnum` is the **1-based** attribute number of the column (field) caller wants.
/// - `tupleDesc` is a pointer to the structure describing the row and all its fields.
///
/// These things must complement each other correctly
#[inline(always)]
pub unsafe fn heap_getattr(
    tup: *mut HeapTupleData,
    attnum: i32,
    tupleDesc: TupleDesc,
    isnull: &mut bool,
) -> Datum {
    // static inline Datum
    // heap_getattr(HeapTuple tup, int attnum, TupleDesc tupleDesc, bool *isnull)
    // {
    // 	if (attnum > 0)
    // 	{
    // 		if (attnum > (int) HeapTupleHeaderGetNatts(tup->t_data))
    // 			return getmissingattr(tupleDesc, attnum, isnull);
    // 		else
    // 			return fastgetattr(tup, attnum, tupleDesc, isnull);
    // 	}
    // 	else
    // 		return heap_getsysattr(tup, attnum, tupleDesc, isnull);
    // }

    unsafe {
        // SAFETY:  caller has asserted that `tup` and `tupleDesc` are valid pointers
        if attnum > 0 {
            if attnum > HeapTupleHeaderGetNatts((*tup).t_data) as i32 {
                getmissingattr(tupleDesc, attnum, isnull)
            } else {
                fastgetattr(tup, attnum, tupleDesc, isnull)
            }
        } else {
            heap_getsysattr(tup, attnum, tupleDesc, isnull)
        }
    }
}

/// Fetch a user attribute's value as a Datum (might be either a
/// value, or a pointer into the data area of the tuple).
///
/// # Safety
///
/// This must not be used when a system attribute might be requested.
/// Furthermore, the passed attnum MUST be valid.  Use [heap_getattr]
/// instead, if in doubt.
///
/// # Panics
///
/// Will panic if `attnum` is less than one
#[inline(always)]
unsafe fn fastgetattr(
    tup: *mut HeapTupleData,
    attnum: i32,
    tupleDesc: TupleDesc,
    isnull: &mut bool,
) -> Datum {
    // static inline Datum
    // fastgetattr(HeapTuple tup, int attnum, TupleDesc tupleDesc, bool *isnull)
    // {
    // 	Assert(attnum > 0);
    //
    // 	*isnull = false;
    // 	if (HeapTupleNoNulls(tup))
    // 	{
    // 		Form_pg_attribute att;
    //
    // 		att = TupleDescAttr(tupleDesc, attnum - 1);
    // 		if (att->attcacheoff >= 0)
    // 			return fetchatt(att, (char *) tup->t_data + tup->t_data->t_hoff +
    // 							att->attcacheoff);
    // 		else
    // 			return nocachegetattr(tup, attnum, tupleDesc);
    // 	}
    // 	else
    // 	{
    // 		if (att_isnull(attnum - 1, tup->t_data->t_bits))
    // 		{
    // 			*isnull = true;
    // 			return (Datum) NULL;
    // 		}
    // 		else
    // 			return nocachegetattr(tup, attnum, tupleDesc);
    // 	}
    // }

    assert!(attnum > 0);

    unsafe {
        *isnull = false;
        if HeapTupleNoNulls(tup) {
            let att = &(*tupleDesc).attrs.as_slice((*tupleDesc).natts as _)[attnum as usize - 1];
            if att.attcacheoff >= 0 {
                let t_data = (*tup).t_data;
                fetchatt(
                    att,
                    t_data
                        .cast::<std::os::raw::c_char>()
                        .add((*t_data).t_hoff as usize + att.attcacheoff as usize),
                )
            } else {
                nocachegetattr(tup, attnum, tupleDesc)
            }
        } else if att_isnull(attnum - 1, (*(*tup).t_data).t_bits.as_ptr()) {
            *isnull = true;
            Datum::from(0) // a NULL pointer
        } else {
            nocachegetattr(tup, attnum, tupleDesc)
        }
    }
}
