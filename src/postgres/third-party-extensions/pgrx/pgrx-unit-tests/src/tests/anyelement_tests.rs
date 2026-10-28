use pgrx::{AnyElement, prelude::*};

#[pg_extern]
fn anyelement_arg(element: AnyElement) -> AnyElement {
    element
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_unit_tests;

    use pgrx::{AnyElement, datum::DatumWithOid, prelude::*};

    #[pg_test]
    fn test_anyelement_arg() -> Result<(), pgrx::spi::Error> {
        let oid = unsafe { DatumWithOid::new(123, AnyElement::type_oid()) };
        let element = Spi::get_one_with_args::<AnyElement>("SELECT anyelement_arg($1);", &[oid])?
            .map(|e| e.datum());

        assert_eq!(element, 123.into_datum());

        Ok(())
    }
}
