use pgrx::prelude::*;

fn main() {}

const DOG_COMPOSITE_TYPE: &str = "Dog";

mod vec {
    // ! TODO: This probably requires the FunctionArguments<'curr, 'fn> type
    use super::*;

    #[pg_extern]
    fn sum_scritches_for_names(
        dogs: Option<Vec<pgrx::composite_type!(DOG_COMPOSITE_TYPE)>>,
    ) -> i32 {
        // Gets resolved to:
        let dogs: Option<Vec<PgHeapTuple<AllocatedByRust>>> = dogs;

        let dogs = dogs.unwrap();
        let mut sum_scritches = 0;
        for dog in dogs {
            let scritches: i32 =
                dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
            sum_scritches += scritches;
        }
        sum_scritches
    }

    #[pg_extern]
    fn sum_scritches_for_names_default(
        dogs: pgrx::default!(Vec<pgrx::composite_type!("Dog")>, "ARRAY[ROW('Nami', 0)]::Dog[]"),
    ) -> i32 {
        // Gets resolved to:
        let dogs: Vec<PgHeapTuple<AllocatedByRust>> = dogs;

        let mut sum_scritches = 0;
        for dog in dogs {
            let scritches: i32 =
                dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
            sum_scritches += scritches;
        }
        sum_scritches
    }

    #[pg_extern]
    fn sum_scritches_for_names_strict(dogs: Vec<pgrx::composite_type!("Dog")>) -> i32 {
        // Gets resolved to:
        let dogs: Vec<PgHeapTuple<AllocatedByRust>> = dogs;

        let mut sum_scritches = 0;
        for dog in dogs {
            let scritches: i32 =
                dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
            sum_scritches += scritches;
        }
        sum_scritches
    }

    #[pg_extern]
    fn sum_scritches_for_names_strict_optional_items(
        dogs: Vec<Option<pgrx::composite_type!("Dog")>>,
    ) -> i32 {
        // Gets resolved to:
        let dogs: Vec<Option<PgHeapTuple<AllocatedByRust>>> = dogs;

        let mut sum_scritches = 0;
        for dog in dogs {
            let dog = dog.unwrap();
            let scritches: i32 =
                dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
            sum_scritches += scritches;
        }
        sum_scritches
    }

    #[pg_extern]
    fn sum_scritches_for_names_default_optional_items(
        dogs: pgrx::default!(
            Vec<Option<pgrx::composite_type!("Dog")>>,
            "ARRAY[ROW('Nami', 0)]::Dog[]"
        ),
    ) -> i32 {
        // Gets resolved to:
        let dogs: Vec<Option<PgHeapTuple<AllocatedByRust>>> = dogs;

        let mut sum_scritches = 0;
        for dog in dogs {
            let dog = dog.unwrap();
            let scritches: i32 =
                dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
            sum_scritches += scritches;
        }
        sum_scritches
    }

    #[pg_extern]
    fn sum_scritches_for_names_optional_items(
        dogs: Option<Vec<Option<pgrx::composite_type!("Dog")>>>,
    ) -> i32 {
        // Gets resolved to:
        let dogs: Option<Vec<Option<PgHeapTuple<AllocatedByRust>>>> = dogs;

        let dogs = dogs.unwrap();
        let mut sum_scritches = 0;
        for dog in dogs {
            let dog = dog.unwrap();
            let scritches: i32 = dog.get_by_name("scritches").unwrap().unwrap();
            sum_scritches += scritches;
        }
        sum_scritches
    }
}

mod array {
    use super::*;

    #[pg_extern]
    fn sum_scritches_for_names_array(
        dogs: Option<pgrx::Array<pgrx::composite_type!("Dog")>>,
    ) -> i32 {
        // Gets resolved to:
        let dogs: Option<pgrx::Array<PgHeapTuple<AllocatedByRust>>> = dogs;

        let dogs = dogs.unwrap();
        let mut sum_scritches = 0;
        for dog in dogs {
            let dog = dog.unwrap();
            let scritches: i32 =
                dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
            sum_scritches += scritches;
        }
        sum_scritches
    }

    #[pg_extern]
    fn sum_scritches_for_names_array_default(
        dogs: pgrx::default!(
            pgrx::Array<pgrx::composite_type!("Dog")>,
            "ARRAY[ROW('Nami', 0)]::Dog[]"
        ),
    ) -> i32 {
        // Gets resolved to:
        let dogs: pgrx::Array<PgHeapTuple<AllocatedByRust>> = dogs;

        let mut sum_scritches = 0;
        for dog in dogs {
            let dog = dog.unwrap();
            let scritches: i32 =
                dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
            sum_scritches += scritches;
        }
        sum_scritches
    }

    #[pg_extern]
    fn sum_scritches_for_names_array_strict(
        dogs: pgrx::Array<pgrx::composite_type!("Dog")>,
    ) -> i32 {
        // Gets resolved to:
        let dogs: pgrx::Array<PgHeapTuple<AllocatedByRust>> = dogs;

        let mut sum_scritches = 0;
        for dog in dogs {
            let dog = dog.unwrap();
            let scritches: i32 =
                dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
            sum_scritches += scritches;
        }
        sum_scritches
    }
}

#[pgrx::pg_schema]
mod tests {
    use pgrx::prelude::*;

    fn test_sum_scritches_for_names() {
        let retval = Spi::get_one::<i32>(
            "
            SELECT sum_scritches_for_names(ARRAY[ROW('Nami', 1), ROW('Brandy', 42)]::Dog[])
        ",
        );
        assert_eq!(retval, Ok(Some(43)));
    }

    fn test_sum_scritches_for_names_default() {
        let retval = Spi::get_one::<i32>(
            "
            SELECT sum_scritches_for_names_default()
        ",
        );
        assert_eq!(retval, Ok(Some(0)));
    }

    fn test_sum_scritches_for_names_strict() {
        let retval = Spi::get_one::<i32>(
            "
            SELECT sum_scritches_for_names_strict(ARRAY[ROW('Nami', 1), ROW('Brandy', 42)]::Dog[])
        ",
        );
        assert_eq!(retval, Ok(Some(43)));
    }

    fn test_sum_scritches_for_names_strict_optional_items() {
        let retval = Spi::get_one::<i32>(
            "
            SELECT sum_scritches_for_names_strict(ARRAY[ROW('Nami', 1), ROW('Brandy', 42)]::Dog[])
        ",
        );
        assert_eq!(retval, Ok(Some(43)));
    }

    fn test_sum_scritches_for_names_default_optional_items() {
        let retval = Spi::get_one::<i32>(
            "
            SELECT sum_scritches_for_names_default_optional_items()
        ",
        );
        assert_eq!(retval, Ok(Some(0)));
    }

    fn test_sum_scritches_for_names_optional_items() {
        let retval = Spi::get_one::<i32>("
            SELECT sum_scritches_for_names_optional_items(ARRAY[ROW('Nami', 1), ROW('Brandy', 42)]::Dog[])
        ");
        assert_eq!(retval, Ok(Some(43)));
    }

    fn test_sum_scritches_for_names_array() {
        let retval = Spi::get_one::<i32>(
            "
            SELECT sum_scritches_for_names_array(ARRAY[ROW('Nami', 1), ROW('Brandy', 42)]::Dog[])
        ",
        );
        assert_eq!(retval, Ok(Some(43)));
    }

    fn test_sum_scritches_for_names_array_default() {
        let retval = Spi::get_one::<i32>(
            "
            SELECT sum_scritches_for_names_array_default()
        ",
        );
        assert_eq!(retval, Ok(Some(0)));
    }

    fn test_sum_scritches_for_names_array_strict() {
        let retval = Spi::get_one::<i32>("
            SELECT sum_scritches_for_names_array_strict(ARRAY[ROW('Nami', 1), ROW('Brandy', 42)]::Dog[])
        ");
        assert_eq!(retval, Ok(Some(43)));
    }
}
