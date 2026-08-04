use arrow::buffer::{NullBuffer, OffsetBuffer, ScalarBuffer};

use crate::type_compat::map::Map;

pub(crate) fn arrow_map_offsets(maps: &Vec<Option<Map>>) -> (OffsetBuffer<i32>, NullBuffer) {
    let mut offsets = vec![0];
    let mut nulls = vec![];

    for map in maps {
        if let Some(map) = map {
            let len = map.entries.len() as i32;
            offsets.push(offsets.last().expect("failed to get last map offset") + len);
            nulls.push(true);
        } else {
            offsets.push(*offsets.last().expect("failed to get last map offset"));
            nulls.push(false);
        }
    }

    let offsets = OffsetBuffer::new(ScalarBuffer::from(offsets));
    let nulls = NullBuffer::from(nulls);

    (offsets, nulls)
}

pub(crate) fn arrow_array_offsets<T>(
    pg_array: &Vec<Option<Vec<Option<T>>>>,
) -> (OffsetBuffer<i32>, NullBuffer) {
    let mut nulls = vec![];
    let mut offsets = vec![0];

    for pg_array in pg_array {
        if let Some(pg_array) = pg_array {
            let len = pg_array.len() as i32;
            offsets.push(offsets.last().expect("failed to get last array offset") + len);
            nulls.push(true);
        } else {
            offsets.push(*offsets.last().expect("failed to get last array offset"));
            nulls.push(false);
        }
    }

    let offsets = arrow::buffer::OffsetBuffer::new(arrow::buffer::ScalarBuffer::from(offsets));
    let nulls = arrow::buffer::NullBuffer::from(nulls);

    (offsets, nulls)
}
