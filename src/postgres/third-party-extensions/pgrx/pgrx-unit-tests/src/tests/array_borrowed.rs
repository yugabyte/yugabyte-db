//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#![allow(unused_imports)]
use core::ffi::CStr;
use pgrx::Json;
use pgrx::PostgresEnum;
use pgrx::array::{FlatArray, RawArray};
use pgrx::memcx::MemCx;
use pgrx::nullable::Nullable;
use pgrx::palloc::PBox;
use pgrx::prelude::*;
use serde::Serialize;
use serde_json::json;

#[pg_extern]
fn return_box_array<'mcx>(memcx: &MemCx<'mcx>) -> PBox<'mcx, FlatArray<'mcx, i32>> {
    FlatArray::new_zeroed_in([1; 1], false, memcx).unwrap()
}

#[pg_extern(name = "borrow_sum_array")]
fn borrow_sum_array_i32(values: &FlatArray<'_, i32>) -> i32 {
    // we implement it this way so we can trap an overflow (as we have a test for this) and
    // catch it correctly in both --debug and --release modes
    let mut sum = 0_i32;
    for v in values {
        let v = v.into_option().copied().unwrap_or(0);
        let (val, overflow) = sum.overflowing_add(v);
        if overflow {
            panic!("attempt to add with overflow");
        } else {
            sum = val;
        }
    }
    sum
}

#[pg_extern(name = "borrow_sum_array")]
fn borrow_sum_array_i64(values: &FlatArray<'_, i64>) -> i64 {
    values.iter().map(|v| v.into_option().copied().unwrap_or(0i64)).sum()
}

#[pg_extern]
fn borrow_count_true(values: &FlatArray<'_, bool>) -> i32 {
    values.iter().filter(|b| b.into_option().copied().unwrap_or(false)).count() as i32
}

#[pg_extern]
fn borrow_count_nulls(values: &FlatArray<'_, i32>) -> i32 {
    values.iter().map(|v| v.as_option().is_none()).filter(|v| *v).count() as i32
}

#[pg_extern]
fn borrow_optional_array_arg(values: Option<&FlatArray<'_, f32>>) -> f32 {
    values.unwrap().iter().map(|v| v.into_option().copied().unwrap_or(0f32)).sum()
}

#[pg_extern]
fn borrow_optional_array_with_default(
    values: default!(Option<&FlatArray<'_, i32>>, "NULL"),
) -> i32 {
    values.unwrap().iter().map(|v| v.into_option().copied().unwrap_or(0)).sum()
}

// FIXME: replace with Text
// #[pg_extern]
// fn borrow_serde_serialize_array<'dat>(values: &FlatArray<'dat, &'dat str>) -> Json {
//     Json(json! { { "values": values } })
// }

#[pg_extern]
fn borrow_serde_serialize_array_i32(values: &FlatArray<'_, i32>) -> Json {
    let values = values.into_iter().map(|v| v.into_option()).collect::<Vec<Option<_>>>();
    Json(json! { { "values": values } })
}

#[pg_extern]
fn borrow_return_text_array() -> Vec<&'static str> {
    vec!["a", "b", "c", "d"]
}

#[pg_extern]
fn borrow_return_zero_length_vec() -> Vec<i32> {
    Vec::new()
}

#[pg_extern]
fn borrow_get_arr_nelems(arr: &FlatArray<'_, i32>) -> libc::c_int {
    arr.nelems() as _
}

#[pg_extern]
fn borrow_get_arr_data_ptr_nth_elem(arr: &FlatArray<'_, i32>, elem: i32) -> Option<i32> {
    arr.get(elem as usize).unwrap().into_option().copied()
}

#[pg_extern]
fn borrow_display_get_arr_nullbitmap(arr: &FlatArray<'_, i32>) -> String {
    if let Some(slice) = arr.nullbitmap_bytes() {
        // SAFETY: If the test has gotten this far, the ptr is good for 0+ bytes,
        // so reborrow NonNull<[u8]> as &[u8] for the hot second we're looking at it.
        // might panic if the array is len 0
        format!("{:#010b}", slice[0])
    } else {
        String::from("")
    }
}

#[pg_extern]
fn borrow_get_arr_ndim(arr: &FlatArray<'_, i32>) -> libc::c_int {
    // SAFETY: This is a valid FlatArrayType and it's just a field access.
    arr.ndims() as libc::c_int
}

// This deliberately iterates the FlatArray.
// Because FlatArray::iter currently iterates the FlatArray as Datums, this is guaranteed to be "bug-free" regarding size.
#[pg_extern]
fn borrow_arr_mapped_vec(arr: &FlatArray<'_, i32>) -> Vec<i32> {
    arr.iter().filter_map(|x| x.into_option()).copied().collect()
}

/// Naive conversion.
#[pg_extern]
#[allow(deprecated)]
fn borrow_arr_into_vec(arr: &FlatArray<'_, i32>) -> Vec<i32> {
    arr.iter_non_null().copied().collect()
}

#[pg_extern]
#[allow(deprecated)]
fn borrow_arr_sort_uniq(arr: &FlatArray<'_, i32>) -> Vec<i32> {
    let mut v: Vec<i32> = arr.iter_non_null().copied().collect();
    v.sort();
    v.dedup();
    v
}

#[pg_extern]
fn borrow_validate_cstring_array(
    a: &FlatArray<'_, CStr>,
) -> std::result::Result<bool, Box<dyn std::error::Error>> {
    assert_eq!(
        a.iter().map(|v| v.into_option()).collect::<Vec<_>>(),
        vec![
            Some(c"one"),
            Some(c"two"),
            None,
            Some(c"four"),
            Some(c"five"),
            None,
            Some(c"seven"),
            None,
            None
        ]
    );
    Ok(true)
}

#[pg_extern]
fn new_array<'cx>(memcx: &'cx MemCx<'cx>) -> PBox<'cx, FlatArray<'cx, i32>> {
    let mut array = FlatArray::new_zeroed_in([3, 4], false, memcx).unwrap();
    for (i, elem) in array.as_non_null_slice_mut().unwrap().iter_mut().enumerate() {
        *elem = i as i32;
    }
    array
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    use crate as pgrx_unit_tests;

    use super::*;
    use pgrx::array::ArrayAllocError;
    use pgrx::datum::DatumWithOid;
    use pgrx::memcx;
    use pgrx::prelude::*;
    use pgrx::{IntoDatum, Json};
    use serde_json::json;

    #[pg_test]
    fn read_array_back() {
        memcx::current_context(|memcx| {
            let array = new_array(memcx);
            for (i, elem) in array.iter_non_null().enumerate() {
                assert_eq!(i as i32, *elem);
            }
            let elems = array.nelems();
            assert_eq!(array.iter_non_null().count(), elems);
        })
    }

    #[pg_test]
    fn error_cases() {
        memcx::current_context(|memcx| {
            let result = FlatArray::<i32>::new_zeroed_in([i32::MAX as usize], false, memcx);
            assert!(match result {
                Err(ArrayAllocError::TooManyElems) => true,
                _ => false,
            });
            let result = FlatArray::<i32>::new_zeroed_in([i32::MAX as usize + 1], false, memcx);
            assert!(match result {
                Err(ArrayAllocError::TooManyElems) => true,
                _ => false,
            });
            let result = FlatArray::<i32>::new_zeroed_in([0], false, memcx);
            assert!(match result {
                Err(ArrayAllocError::ZeroLenDim) => true,
                _ => false,
            });
            let result = FlatArray::<i64>::new_zeroed_in(
                // this is just under the element limit, but it remains too big to allocate
                [0x3fffffff / size_of::<pg_sys::Datum>() - 1],
                true,
                memcx,
            );
            assert!(match result {
                Err(ArrayAllocError::TooManyBytes) => true,
                _ => false,
            });
        })
    }

    #[pg_test]
    fn borrow_test_sum_array_i32() {
        let sum = Spi::get_one::<i32>("SELECT borrow_sum_array(ARRAY[1,2,3]::integer[])");
        assert_eq!(sum, Ok(Some(6)));
    }

    #[pg_test]
    fn borrow_test_sum_array_i64() {
        let sum = Spi::get_one::<i64>("SELECT borrow_sum_array(ARRAY[1,2,3]::bigint[])");
        assert_eq!(sum, Ok(Some(6)));
    }

    #[pg_test(expected = "attempt to add with overflow")]
    fn borrow_test_sum_array_i32_overflow() -> Result<Option<i64>, pgrx::spi::Error> {
        Spi::get_one::<i64>(
            "SELECT borrow_sum_array(a) FROM (SELECT array_agg(s) a FROM generate_series(1, 1000000) s) x;",
        )
    }

    #[pg_test]
    fn borrow_test_count_true() {
        let cnt = Spi::get_one::<i32>("SELECT borrow_count_true(ARRAY[true, true, false, true])");
        assert_eq!(cnt, Ok(Some(3)));
    }

    #[pg_test]
    fn borrow_test_count_nulls() {
        let cnt =
            Spi::get_one::<i32>("SELECT borrow_count_nulls(ARRAY[NULL, 1, 2, NULL]::integer[])");
        assert_eq!(cnt, Ok(Some(2)));
    }

    #[pg_test]
    fn borrow_test_optional_array() {
        let sum = Spi::get_one::<f32>("SELECT borrow_optional_array_arg(ARRAY[1,2,3]::real[])");
        assert_eq!(sum, Ok(Some(6f32)));
    }

    // TODO: fix this test by redesigning SPI.
    // #[pg_test]
    // fn borrow_test_serde_serialize_array() -> Result<(), pgrx::spi::Error> {
    //     let json = Spi::get_one::<Json>(
    //         "SELECT borrow_serde_serialize_array(ARRAY['one', null, 'two', 'three'])",
    //     )?
    //     .expect("returned json was null");
    //     assert_eq!(json.0, json! {{"values": ["one", null, "two", "three"]}});
    //     Ok(())
    // }

    #[pg_test]
    fn borrow_test_optional_array_with_default() {
        let sum = Spi::get_one::<i32>("SELECT borrow_optional_array_with_default(ARRAY[1,2,3])");
        assert_eq!(sum, Ok(Some(6)));
    }

    #[pg_test]
    fn borrow_test_serde_serialize_array_i32() -> Result<(), pgrx::spi::Error> {
        let json = Spi::get_one::<Json>(
            "SELECT borrow_serde_serialize_array_i32(ARRAY[1, null, 2, 3, null, 4, 5])",
        )?
        .expect("returned json was null");
        assert_eq!(json.0, json! {{"values": [1,null,2,3,null,4, 5]}});
        Ok(())
    }

    #[pg_test]
    fn borrow_test_return_text_array() {
        let rc = Spi::get_one::<bool>("SELECT ARRAY['a', 'b', 'c', 'd'] = return_text_array();");
        assert_eq!(rc, Ok(Some(true)));
    }

    #[pg_test]
    fn borrow_test_return_zero_length_vec() {
        let rc = Spi::get_one::<bool>("SELECT ARRAY[]::integer[] = return_zero_length_vec();");
        assert_eq!(rc, Ok(Some(true)));
    }

    #[pg_test]
    fn borrow_test_slice_to_array() -> Result<(), pgrx::spi::Error> {
        let owned_vec = vec![Some(1), None, Some(2), Some(3), None, Some(4), Some(5)];
        let args = unsafe {
            [DatumWithOid::new(
                owned_vec.as_slice().into_datum(),
                PgBuiltInOids::INT4ARRAYOID.value(),
            )]
        };
        let json = Spi::connect(|client| {
            client
                .select("SELECT borrow_serde_serialize_array_i32($1)", None, &args)?
                .first()
                .get_one::<Json>()
        })?
        .expect("Failed to return json even though it's right there ^^");
        assert_eq!(json.0, json! {{"values": [1, null, 2, 3, null, 4, 5]}});
        Ok(())
    }

    #[pg_test]
    fn borrow_test_arr_data_ptr() {
        let len = Spi::get_one::<i32>("SELECT borrow_get_arr_nelems('{1,2,3,4,5}'::int[])");
        assert_eq!(len, Ok(Some(5)));
    }

    #[pg_test]
    fn borrow_test_get_arr_data_ptr_nth_elem() {
        let nth =
            Spi::get_one::<i32>("SELECT borrow_get_arr_data_ptr_nth_elem('{1,2,3,4,5}'::int[], 2)");
        assert_eq!(nth, Ok(Some(3)));
    }

    #[pg_test]
    fn borrow_test_display_get_arr_nullbitmap() -> Result<(), pgrx::spi::Error> {
        let bitmap_str = Spi::get_one::<String>(
            "SELECT borrow_display_get_arr_nullbitmap(ARRAY[1,NULL,3,NULL,5]::int[])",
        )?
        .expect("datum was null");

        assert_eq!(bitmap_str, "0b00010101");

        let bitmap_str = Spi::get_one::<String>(
            "SELECT borrow_display_get_arr_nullbitmap(ARRAY[1,2,3,4,5]::int[])",
        )?
        .expect("datum was null");

        assert_eq!(bitmap_str, "");
        Ok(())
    }

    #[pg_test]
    fn borrow_test_get_arr_ndim() -> Result<(), pgrx::spi::Error> {
        let ndim = Spi::get_one::<i32>("SELECT borrow_get_arr_ndim(ARRAY[1,2,3,4,5]::int[])")?
            .expect("datum was null");

        assert_eq!(ndim, 1);

        let ndim = Spi::get_one::<i32>("SELECT borrow_get_arr_ndim('{{1,2,3},{4,5,6}}'::int[])")?
            .expect("datum was null");

        assert_eq!(ndim, 2);
        Ok(())
    }

    #[pg_test]
    fn borrow_test_arr_to_vec() {
        let result =
            Spi::get_one::<Vec<i32>>("SELECT borrow_arr_mapped_vec(ARRAY[3,2,2,1]::integer[])");
        let other =
            Spi::get_one::<Vec<i32>>("SELECT borrow_arr_into_vec(ARRAY[3,2,2,1]::integer[])");
        // One should be equivalent to the canonical form.
        assert_eq!(result, Ok(Some(vec![3, 2, 2, 1])));
        // And they should be equal to each other.
        assert_eq!(result, other);
    }

    #[pg_test]
    fn borrow_test_arr_sort_uniq() {
        let result =
            Spi::get_one::<Vec<i32>>("SELECT borrow_arr_sort_uniq(ARRAY[3,2,2,1]::integer[])");
        assert_eq!(result, Ok(Some(vec![1, 2, 3])));
    }

    #[pg_test]
    fn borrow_test_arr_sort_uniq_with_null() {
        let result =
            Spi::get_one::<Vec<i32>>("SELECT borrow_arr_sort_uniq(ARRAY[3,2,NULL,2,1]::integer[])");
        assert_eq!(result, Ok(Some(vec![1, 2, 3])));
    }

    #[pg_test]
    fn borrow_test_cstring_array() -> Result<(), pgrx::spi::Error> {
        let strings = Spi::get_one::<bool>("SELECT borrow_validate_cstring_array(ARRAY['one', 'two', NULL, 'four', 'five', NULL, 'seven', NULL, NULL]::cstring[])")?.expect("datum was NULL");
        assert_eq!(strings, true);
        Ok(())
    }

    // FIXME: lol SPI
    // #[pg_test]
    // fn borrow_test_f64_slice() -> Result<(), Box<dyn std::error::Error>> {
    //     let array = Spi::get_one::<&FlatArray<'_, f64>>("SELECT ARRAY[1.0, 2.0, 3.0]::float8[]")?
    //         .expect("datum was null");
    //     assert_eq!(array.as_slice()?, &[1.0, 2.0, 3.0]);
    //     Ok(())
    // }

    // #[pg_test]
    // fn borrow_test_f32_slice() -> Result<(), Box<dyn std::error::Error>> {
    //     let array = Spi::get_one::<&FlatArray<'_, f32>>("SELECT ARRAY[1.0, 2.0, 3.0]::float4[]")?
    //         .expect("datum was null");
    //     assert_eq!(array.as_slice()?, &[1.0, 2.0, 3.0]);
    //     Ok(())
    // }

    // #[pg_test]
    // fn borrow_test_i64_slice() -> Result<(), Box<dyn std::error::Error>> {
    //     let array = Spi::get_one::<&FlatArray<'_, i64>>("SELECT ARRAY[1, 2, 3]::bigint[]")?
    //         .expect("datum was null");
    //     assert_eq!(array.as_slice()?, &[1, 2, 3]);
    //     Ok(())
    // }

    // #[pg_test]
    // fn borrow_test_i32_slice() -> Result<(), Box<dyn std::error::Error>> {
    //     let array = Spi::get_one::<&FlatArray<'_, i32>>("SELECT ARRAY[1, 2, 3]::integer[]")?
    //         .expect("datum was null");
    //     assert_eq!(array.as_slice()?, &[1, 2, 3]);
    //     Ok(())
    // }

    // #[pg_test]
    // fn borrow_test_i16_slice() -> Result<(), Box<dyn std::error::Error>> {
    //     let array = Spi::get_one::<&FlatArray<'_, i16>>("SELECT ARRAY[1, 2, 3]::smallint[]")?
    //         .expect("datum was null");
    //     assert_eq!(array.as_slice()?, &[1, 2, 3]);
    //     Ok(())
    // }

    // #[pg_test]
    // fn borrow_test_slice_with_null() -> Result<(), Box<dyn std::error::Error>> {
    //     let array = Spi::get_one::<FlatArray<'_, i16>>("SELECT ARRAY[1, 2, 3, NULL]::smallint[]")?
    //         .expect("datum was null");
    //     assert_eq!(array.as_slice(), Err(FlatArraySliceError::ContainsNulls));
    //     Ok(())
    // }

    // #[pg_test]
    // fn borrow_test_array_of_points() -> Result<(), Box<dyn std::error::Error>> {
    //     let points: &FlatArray<'_, pg_sys::Point> = Spi::get_one(
    //         "SELECT ARRAY['(1,1)', '(2, 2)', '(3,3)', '(4,4)', NULL, '(5,5)']::point[]",
    //     )?
    //     .unwrap();
    //     let points = points.into_iter().collect::<Vec<_>>();
    //     let expected = vec![
    //         Some(pg_sys::Point { x: 1.0, y: 1.0 }),
    //         Some(pg_sys::Point { x: 2.0, y: 2.0 }),
    //         Some(pg_sys::Point { x: 3.0, y: 3.0 }),
    //         Some(pg_sys::Point { x: 4.0, y: 4.0 }),
    //         None,
    //         Some(pg_sys::Point { x: 5.0, y: 5.0 }),
    //     ];

    //     for (p, expected) in points.into_iter().zip(expected.into_iter()) {
    //         match (p, expected) {
    //             (Some(l), Some(r)) => {
    //                 assert_eq!(l.x, r.x);
    //                 assert_eq!(l.y, r.y);
    //             }
    //             (None, None) => (),
    //             _ => panic!("points not equal"),
    //         }
    //     }
    //     Ok(())
    // }

    // FIXME: needs to be type-subbed to a stringly type and SPI needs to make sense
    // #[pg_test]
    // fn borrow_test_text_array_as_vec_string() -> Result<(), Box<dyn std::error::Error>> {
    //     let a = Spi::get_one::<&FlatArray<'_, String>>(
    //         "SELECT ARRAY[NULL, NULL, NULL, NULL, 'the fifth element']::text[]",
    //     )?
    //     .expect("spi result was NULL")
    //     .into_iter()
    //     .collect::<Vec<_>>();
    //     assert_eq!(a, vec![None, None, None, None, Some(String::from("the fifth element"))]);
    //     Ok(())
    // }

    // FIXME: needs to be type-subbed to a stringly type
    // #[pg_test]
    // fn borrow_test_text_array_iter() -> Result<(), Box<dyn std::error::Error>> {
    //     let a = Spi::get_one::<&FlatArray<'_, String>>(
    //         "SELECT ARRAY[NULL, NULL, NULL, NULL, 'the fifth element']::text[]",
    //     )?
    //     .expect("spi result was NULL");

    //     let mut iter = a.iter();

    //     assert_eq!(iter.next(), Some(None));
    //     assert_eq!(iter.next(), Some(None));
    //     assert_eq!(iter.next(), Some(None));
    //     assert_eq!(iter.next(), Some(None));
    //     assert_eq!(iter.next(), Some(Some(String::from("the fifth element"))));
    //     assert_eq!(iter.next(), None);

    //     Ok(())
    // }

    // FIXME: Needs to be type-subbed to a stringly type
    // #[pg_test]
    // fn borrow_test_text_array_via_getter() -> Result<(), Box<dyn std::error::Error>> {
    //     let a = Spi::get_one::<&FlatArray<'_, String>>(
    //         "SELECT ARRAY[NULL, NULL, NULL, NULL, 'the fifth element']::text[]",
    //     )?
    //     .expect("spi result was NULL");

    //     assert_eq!(a.nth(0), Some(None));
    //     assert_eq!(a.nth(1), Some(None));
    //     assert_eq!(a.nth(2), Some(None));
    //     assert_eq!(a.nth(3), Some(None));
    //     assert_eq!(a.nth(4), Some(Some(String::from("the fifth element"))));
    //     assert_eq!(a.nth(5), None);

    //     Ok(())
    // }
}
