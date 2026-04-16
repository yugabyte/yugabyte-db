//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use std::hash::{Hash, Hasher};

use crate::{direct_function_call, pg_sys, AnyNumeric, Numeric};

impl Hash for AnyNumeric {
    #[inline]
    fn hash<H: Hasher>(&self, state: &mut H) {
        unsafe {
            let hash = direct_function_call(pg_sys::hash_numeric, &[self.as_datum()]).unwrap();
            state.write_i32(hash)
        }
    }
}

impl<const P: u32, const S: u32> Hash for Numeric<P, S> {
    #[inline]
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.as_anynumeric().hash(state)
    }
}
