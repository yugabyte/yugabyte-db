//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::{pg_sys, FromDatum, IntoDatum};

impl<A, B> IntoDatum for (Option<A>, Option<B>)
where
    A: IntoDatum,
    B: IntoDatum,
{
    fn into_datum(self) -> Option<pg_sys::Datum> {
        let arr = [self.0.into_datum(), self.1.into_datum()];
        arr.into_datum()
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::Oid::INVALID
    }
}

impl<A, B, C> IntoDatum for (Option<A>, Option<B>, Option<C>)
where
    A: IntoDatum,
    B: IntoDatum,
    C: IntoDatum,
{
    fn into_datum(self) -> Option<pg_sys::Datum> {
        let arr = [self.0.into_datum(), self.1.into_datum(), self.2.into_datum()];
        arr.into_datum()
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::Oid::INVALID
    }
}

impl<A, B> FromDatum for (Option<A>, Option<B>)
where
    A: FromDatum + IntoDatum,
    B: FromDatum + IntoDatum,
{
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        typoid: pg_sys::Oid,
    ) -> Option<Self>
    where
        Self: Sized,
    {
        let mut vec =
            Vec::<Option<pg_sys::Datum>>::from_polymorphic_datum(datum, is_null, typoid).unwrap();
        let b = vec.pop().unwrap();
        let a = vec.pop().unwrap();

        Some((
            a.and_then(|a| A::from_polymorphic_datum(a, false, A::type_oid())),
            b.and_then(|b| B::from_polymorphic_datum(b, false, B::type_oid())),
        ))
    }
}

impl<A, B, C> FromDatum for (Option<A>, Option<B>, Option<C>)
where
    A: FromDatum + IntoDatum,
    B: FromDatum + IntoDatum,
    C: FromDatum + IntoDatum,
{
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        typoid: pg_sys::Oid,
    ) -> Option<Self>
    where
        Self: Sized,
    {
        let mut vec =
            Vec::<Option<pg_sys::Datum>>::from_polymorphic_datum(datum, is_null, typoid).unwrap();
        let c = vec.pop().unwrap();
        let b = vec.pop().unwrap();
        let a = vec.pop().unwrap();

        Some((
            a.and_then(|a| A::from_polymorphic_datum(a, false, A::type_oid())),
            b.and_then(|b| B::from_polymorphic_datum(b, false, B::type_oid())),
            c.and_then(|c| C::from_polymorphic_datum(c, false, C::type_oid())),
        ))
    }
}
