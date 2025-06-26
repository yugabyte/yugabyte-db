//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
// This is used by some, but not all, examples below.
const DOG_COMPOSITE_TYPE: &str = "Dog";

use pgrx::pgbox::AllocatedByRust;
use pgrx::prelude::*;
use pgrx::VariadicArray;

extension_sql!(
    r#"
CREATE TYPE Dog AS (
    name TEXT,
    scritches INT
);

CREATE TYPE Cat AS (
    name TEXT,
    boops INT
);

CREATE TYPE Fish AS (
    name TEXT,
    bloops INT
);

CREATE TYPE AnimalFriendshipEdge AS (
    friend_1_name TEXT,
    friend_2_name TEXT
);
"#,
    name = "create_composites",
    bootstrap
);

// As Arguments
mod arguments {
    use super::*;

    mod singleton {
        //! Originally, these tested taking HeapTuple<'a, _> -> &'a str, but that allows data to
        //! escape the lifetime of its root datum which prevents correct reasoning about lifetimes.
        //! Perhaps they could eventually test &'tup HeapTuple<'mcx, _> -> &'tup str?

        use super::*;
        #[pg_extern]
        fn gets_name_field(dog: Option<pgrx::composite_type!("Dog")>) -> Option<String> {
            // Gets resolved to:
            let dog: Option<PgHeapTuple<AllocatedByRust>> = dog;

            dog?.get_by_name("name").ok()?
        }

        #[pg_extern]
        fn gets_name_field_default(
            dog: default!(pgrx::composite_type!("Dog"), "ROW('Nami', 0)::Dog"),
        ) -> String {
            // Gets resolved to:
            let dog: PgHeapTuple<AllocatedByRust> = dog;

            dog.get_by_name("name").unwrap().unwrap()
        }

        #[pg_extern]
        fn gets_name_field_strict(dog: pgrx::composite_type!("Dog")) -> String {
            // Gets resolved to:
            let dog: PgHeapTuple<AllocatedByRust> = dog;

            dog.get_by_name("name").unwrap().unwrap()
        }
    }

    mod variadic_array {
        //! TODO: biggest problem here is the inability to use
        //! ```ignore
        //! for dog in dogs {
        //!     todo!()
        //! }
        //! ```
        use super::*;

        #[pg_extern]
        fn gets_name_field_variadic(
            dogs: VariadicArray<pgrx::composite_type!(DOG_COMPOSITE_TYPE)>,
        ) -> Vec<String> {
            // Gets resolved to:
            let dogs: VariadicArray<PgHeapTuple<AllocatedByRust>> = dogs;

            let mut names = Vec::with_capacity(dogs.len());
            for dog in dogs.iter() {
                let dog = dog.unwrap();
                let name = dog.get_by_name("name").unwrap().unwrap();
                names.push(name);
            }
            names
        }

        /// TODO: add test which actually calls this maybe???
        #[pg_extern]
        fn gets_name_field_default_variadic(
            dogs: default!(
                VariadicArray<pgrx::composite_type!("Dog")>,
                "ARRAY[ROW('Nami', 0)]::Dog[]"
            ),
        ) -> Vec<String> {
            // Gets resolved to:
            let dogs: VariadicArray<PgHeapTuple<AllocatedByRust>> = dogs;

            let mut names = Vec::with_capacity(dogs.len());
            for dog in dogs.iter() {
                let dog = dog.unwrap();
                let name = dog.get_by_name("name").unwrap().unwrap();
                names.push(name);
            }
            names
        }

        #[pg_extern]
        fn gets_name_field_strict_variadic(
            dogs: VariadicArray<pgrx::composite_type!("Dog")>,
        ) -> Vec<String> {
            // Gets resolved to:
            let dogs: VariadicArray<PgHeapTuple<AllocatedByRust>> = dogs;

            let mut names = Vec::with_capacity(dogs.len());
            for dog in dogs.iter() {
                let dog = dog.unwrap();
                let name = dog.get_by_name("name").unwrap().unwrap();
                names.push(name);
            }
            names
        }
    }

    mod vec {
        //! TODO: This probably requires the FunctionArguments<'curr, 'fn> type
        // use super::*;

        // #[pg_extern]
        // fn sum_scritches_for_names(
        //     dogs: Option<Vec<pgrx::composite_type!(DOG_COMPOSITE_TYPE)>>,
        // ) -> i32 {
        //     // Gets resolved to:
        //     let dogs: Option<Vec<PgHeapTuple<AllocatedByRust>>> = dogs;

        //     let dogs = dogs.unwrap();
        //     let mut sum_scritches = 0;
        //     for dog in dogs {
        //         let scritches: i32 =
        //             dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
        //         sum_scritches += scritches;
        //     }
        //     sum_scritches
        // }

        // #[pg_extern]
        // fn sum_scritches_for_names_default(
        //     dogs: pgrx::default!(Vec<pgrx::composite_type!("Dog")>, "ARRAY[ROW('Nami', 0)]::Dog[]"),
        // ) -> i32 {
        //     // Gets resolved to:
        //     let dogs: Vec<PgHeapTuple<AllocatedByRust>> = dogs;

        //     let mut sum_scritches = 0;
        //     for dog in dogs {
        //         let scritches: i32 =
        //             dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
        //         sum_scritches += scritches;
        //     }
        //     sum_scritches
        // }

        // #[pg_extern]
        // fn sum_scritches_for_names_strict(dogs: Vec<pgrx::composite_type!("Dog")>) -> i32 {
        //     // Gets resolved to:
        //     let dogs: Vec<PgHeapTuple<AllocatedByRust>> = dogs;

        //     let mut sum_scritches = 0;
        //     for dog in dogs {
        //         let scritches: i32 =
        //             dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
        //         sum_scritches += scritches;
        //     }
        //     sum_scritches
        // }

        // #[pg_extern]
        // fn sum_scritches_for_names_strict_optional_items(
        //     dogs: Vec<Option<pgrx::composite_type!("Dog")>>,
        // ) -> i32 {
        //     // Gets resolved to:
        //     let dogs: Vec<Option<PgHeapTuple<AllocatedByRust>>> = dogs;

        //     let mut sum_scritches = 0;
        //     for dog in dogs {
        //         let dog = dog.unwrap();
        //         let scritches: i32 =
        //             dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
        //         sum_scritches += scritches;
        //     }
        //     sum_scritches
        // }

        // #[pg_extern]
        // fn sum_scritches_for_names_default_optional_items(
        //     dogs: pgrx::default!(
        //         Vec<Option<pgrx::composite_type!("Dog")>>,
        //         "ARRAY[ROW('Nami', 0)]::Dog[]"
        //     ),
        // ) -> i32 {
        //     // Gets resolved to:
        //     let dogs: Vec<Option<PgHeapTuple<AllocatedByRust>>> = dogs;

        //     let mut sum_scritches = 0;
        //     for dog in dogs {
        //         let dog = dog.unwrap();
        //         let scritches: i32 =
        //             dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
        //         sum_scritches += scritches;
        //     }
        //     sum_scritches
        // }

        // #[pg_extern]
        // fn sum_scritches_for_names_optional_items(
        //     dogs: Option<Vec<Option<pgrx::composite_type!("Dog")>>>,
        // ) -> i32 {
        //     // Gets resolved to:
        //     let dogs: Option<Vec<Option<PgHeapTuple<AllocatedByRust>>>> = dogs;

        //     let dogs = dogs.unwrap();
        //     let mut sum_scritches = 0;
        //     for dog in dogs {
        //         let dog = dog.unwrap();
        //         let scritches: i32 = dog.get_by_name("scritches").unwrap().unwrap();
        //         sum_scritches += scritches;
        //     }
        //     sum_scritches
        // }
    }

    // mod array {
    //     use super::*;

    //     #[pg_extern]
    //     fn sum_scritches_for_names_array(
    //         dogs: Option<pgrx::Array<pgrx::composite_type!("Dog")>>,
    //     ) -> i32 {
    //         // Gets resolved to:
    //         let dogs: Option<pgrx::Array<PgHeapTuple<AllocatedByRust>>> = dogs;

    //         let dogs = dogs.unwrap();
    //         let mut sum_scritches = 0;
    //         for dog in dogs {
    //             let dog = dog.unwrap();
    //             let scritches: i32 =
    //                 dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
    //             sum_scritches += scritches;
    //         }
    //         sum_scritches
    //     }

    //     #[pg_extern]
    //     fn sum_scritches_for_names_array_default(
    //         dogs: pgrx::default!(
    //             pgrx::Array<pgrx::composite_type!("Dog")>,
    //             "ARRAY[ROW('Nami', 0)]::Dog[]"
    //         ),
    //     ) -> i32 {
    //         // Gets resolved to:
    //         let dogs: pgrx::Array<PgHeapTuple<AllocatedByRust>> = dogs;

    //         let mut sum_scritches = 0;
    //         for dog in dogs {
    //             let dog = dog.unwrap();
    //             let scritches: i32 =
    //                 dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
    //             sum_scritches += scritches;
    //         }
    //         sum_scritches
    //     }

    //     #[pg_extern]
    //     fn sum_scritches_for_names_array_strict(
    //         dogs: pgrx::Array<pgrx::composite_type!("Dog")>,
    //     ) -> i32 {
    //         // Gets resolved to:
    //         let dogs: pgrx::Array<PgHeapTuple<AllocatedByRust>> = dogs;

    //         let mut sum_scritches = 0;
    //         for dog in dogs {
    //             let dog = dog.unwrap();
    //             let scritches: i32 =
    //                 dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
    //             sum_scritches += scritches;
    //         }
    //         sum_scritches
    //     }
    // }
}

// As return types
mod returning {
    use super::*;

    mod singleton {
        use super::*;

        #[pg_extern]
        fn create_dog(name: String, scritches: i32) -> pgrx::composite_type!('static, "Dog") {
            let mut tuple = PgHeapTuple::new_composite_type("Dog").unwrap();

            tuple.set_by_name("scritches", scritches).unwrap();
            tuple.set_by_name("name", name).unwrap();

            tuple
        }

        #[pg_extern]
        fn scritch(
            maybe_dog: Option<::pgrx::composite_type!("Dog")>,
        ) -> Option<pgrx::composite_type!("Dog")> {
            // Gets resolved to:
            let maybe_dog: Option<PgHeapTuple<AllocatedByRust>> = maybe_dog;

            let maybe_dog = if let Some(mut dog) = maybe_dog {
                dog.set_by_name("scritches", dog.get_by_name::<i32>("scritches").unwrap()).unwrap();
                Some(dog)
            } else {
                None
            };

            maybe_dog
        }

        #[pg_extern]
        fn scritch_strict(dog: ::pgrx::composite_type!("Dog")) -> pgrx::composite_type!("Dog") {
            // Gets resolved to:
            let mut dog: PgHeapTuple<AllocatedByRust> = dog;

            dog.set_by_name("scritches", dog.get_by_name::<i32>("scritches").unwrap()).unwrap();

            dog
        }
    }

    mod vec {
        //! TODO: This probably requires the FunctionArguments<'curr, 'fn> type
        // use super::*;

        // #[pg_extern]
        // fn scritch_all_vec(
        //     maybe_dogs: Option<Vec<::pgrx::composite_type!("Dog")>>,
        // ) -> Option<Vec<::pgrx::composite_type!("Dog")>> {
        //     // Gets resolved to:
        //     let maybe_dogs: Option<Vec<PgHeapTuple<AllocatedByRust>>> = maybe_dogs;

        //     let maybe_dogs = if let Some(mut dogs) = maybe_dogs {
        //         for dog in dogs.iter_mut() {
        //             dog.set_by_name("scritches", dog.get_by_name::<i32>("scritches").unwrap())
        //                 .unwrap();
        //         }
        //         Some(dogs)
        //     } else {
        //         None
        //     };

        //     maybe_dogs
        // }

        // #[pg_extern]
        // fn scritch_all_vec_strict<'a>(
        //     dogs: Vec<::pgrx::composite_type!('a, "Dog")>,
        // ) -> Vec<::pgrx::composite_type!('a, "Dog")> {
        //     // Gets resolved to:
        //     let mut dogs: Vec<PgHeapTuple<AllocatedByRust>> = dogs;

        //     for dog in dogs.iter_mut() {
        //         dog.set_by_name("scritches", dog.get_by_name::<i32>("scritches").unwrap()).unwrap();
        //     }
        //     dogs
        // }

        // #[pg_extern]
        // fn scritch_all_vec_optional_items(
        //     maybe_dogs: Option<Vec<Option<::pgrx::composite_type!("Dog")>>>,
        // ) -> Option<Vec<Option<::pgrx::composite_type!("Dog")>>> {
        //     // Gets resolved to:
        //     let maybe_dogs: Option<Vec<Option<PgHeapTuple<AllocatedByRust>>>> = maybe_dogs;

        //     let maybe_dogs = if let Some(mut dogs) = maybe_dogs {
        //         for dog in dogs.iter_mut() {
        //             if let Some(ref mut dog) = dog {
        //                 dog.set_by_name("scritches", dog.get_by_name::<i32>("scritches").unwrap())
        //                     .unwrap();
        //             }
        //         }
        //         Some(dogs)
        //     } else {
        //         None
        //     };

        //     maybe_dogs
        // }
    }

    // Returning VariadicArray/Array isn't supported, use a Vec.
}

// Just a compile test...
// We don't run these, but we ensure we can build SQL for them
mod sql_generator_tests {
    use super::*;

    // #[pg_extern]
    // fn exotic_signature(
    //     _cats: pgrx::default!(
    //         Option<Vec<Option<::pgrx::composite_type!('static, "Cat")>>>,
    //         "ARRAY[ROW('Sally', 0)]::Cat[]"
    //     ),
    //     _a_single_fish: pgrx::default!(
    //         Option<::pgrx::composite_type!('static, "Fish")>,
    //         "ROW('Bob', 0)::Fish"
    //     ),
    //     _dogs: pgrx::default!(
    //         Option<::pgrx::VariadicArray<::pgrx::composite_type!('static, "Dog")>>,
    //         "ARRAY[ROW('Nami', 0), ROW('Brandy', 0)]::Dog[]"
    //     ),
    // ) -> TableIterator<
    //     'static,
    //     (
    //         name!(dog, Option<::pgrx::composite_type!('static, "Dog")>),
    //         name!(cat, Option<::pgrx::composite_type!('static, "Cat")>),
    //         name!(fish, Option<::pgrx::composite_type!('static, "Fish")>),
    //         name!(
    //             related_edges,
    //             Option<Vec<::pgrx::composite_type!('static, "AnimalFriendshipEdge")>>
    //         ),
    //     ),
    // > {
    //     TableIterator::new(Vec::new().into_iter())
    // }

    #[pg_extern]
    fn iterable_named_table() -> TableIterator<
        'static,
        (
            name!(dog, ::pgrx::composite_type!('static, "Dog")),
            name!(cat, ::pgrx::composite_type!('static, "Cat")),
        ),
    > {
        TableIterator::new(Vec::new().into_iter())
    }

    #[pg_extern]
    fn iterable_named_table_optional_elems() -> TableIterator<
        'static,
        (
            name!(dog, Option<::pgrx::composite_type!('static, "Dog")>),
            name!(cat, Option<::pgrx::composite_type!('static, "Cat")>),
        ),
    > {
        TableIterator::once(Default::default())
    }

    #[pg_extern]
    fn iterable_named_table_array_elems() -> TableIterator<
        'static,
        (
            name!(dog, Vec<::pgrx::composite_type!('static, "Dog")>),
            name!(cat, Vec<::pgrx::composite_type!('static, "Cat")>),
        ),
    > {
        TableIterator::once(Default::default())
    }

    #[pg_extern]
    fn iterable_named_table_optional_array_elems() -> TableIterator<
        'static,
        (
            name!(dog, Option<Vec<::pgrx::composite_type!('static, "Dog")>>),
            name!(cat, Option<Vec<::pgrx::composite_type!('static, "Cat")>>),
        ),
    > {
        TableIterator::once(Default::default())
    }

    #[pg_extern]
    fn iterable_named_table_optional_array_optional_elems() -> TableIterator<
        'static,
        (
            name!(dog, Option<Vec<Option<::pgrx::composite_type!('static, "Dog")>>>),
            name!(cat, Option<Vec<Option<::pgrx::composite_type!('static, "Cat")>>>),
        ),
    > {
        TableIterator::once(Default::default())
    }

    #[allow(unused_parens)]
    #[pg_extern]
    fn return_table_single(
    ) -> TableIterator<'static, (name!(dog, pgrx::composite_type!('static, "Dog")),)> {
        let mut tuple = PgHeapTuple::new_composite_type("Dog").unwrap();

        tuple.set_by_name("scritches", 0).unwrap();
        tuple.set_by_name("name", "Nami").unwrap();

        TableIterator::once((tuple,))
    }

    #[pg_extern]
    fn return_table_single_bare(
    ) -> TableIterator<'static, (name!(dog, pgrx::composite_type!('static, "Dog")),)> {
        let mut tuple = PgHeapTuple::new_composite_type("Dog").unwrap();

        tuple.set_by_name("scritches", 0).unwrap();
        tuple.set_by_name("name", "Nami").unwrap();

        TableIterator::once((tuple,))
    }

    #[pg_extern]
    fn return_table_two() -> TableIterator<
        'static,
        (
            name!(dog, pgrx::composite_type!('static, "Dog")),
            name!(cat, pgrx::composite_type!('static, "Cat")),
        ),
    > {
        let mut dog_tuple = PgHeapTuple::new_composite_type("Dog").unwrap();

        dog_tuple.set_by_name("scritches", 0).unwrap();
        dog_tuple.set_by_name("name", "Brandy").unwrap();

        let mut cat_tuple = PgHeapTuple::new_composite_type("Cat").unwrap();

        cat_tuple.set_by_name("boops", 0).unwrap();
        cat_tuple.set_by_name("name", "Sally").unwrap();

        TableIterator::once((dog_tuple, cat_tuple))
    }

    #[pg_extern]
    fn return_table_two_optional() -> TableIterator<
        'static,
        (
            name!(dog, Option<pgrx::composite_type!('static, "Dog")>),
            name!(cat, Option<pgrx::composite_type!('static, "Cat")>),
        ),
    > {
        TableIterator::once((None, None))
    }

    #[pg_extern]
    fn generate_lots_of_dogs() -> SetOfIterator<'static, pgrx::composite_type!('static, "Dog")> {
        let tuple_desc =
            pgrx::PgTupleDesc::for_composite_type("Dog").expect("Couldn't find TestType");

        let tuples: Vec<PgHeapTuple<'_, AllocatedByRust>> = (0..10_000)
            .into_iter()
            .map(move |i| {
                let datums: Vec<Option<pg_sys::Datum>> =
                    vec!["good boy".into_datum(), i.into_datum()];

                unsafe {
                    PgHeapTuple::from_datums(tuple_desc.clone(), datums)
                        .expect("couldn't get heap tuple")
                }
            })
            .collect();

        SetOfIterator::new(tuples)
    }
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[cfg(test)]
    use crate as pgrx_tests;
    use pgrx::datum::TryFromDatumError;
    use pgrx::heap_tuple::PgHeapTupleError;
    use pgrx::prelude::*;
    use pgrx::AllocatedByRust;
    use std::num::NonZeroUsize;

    #[pg_test]
    fn test_gets_name_field() {
        let retval = Spi::get_one::<&str>(
            "
            SELECT gets_name_field(ROW('Nami', 0)::Dog)
        ",
        );
        assert_eq!(retval, Ok(Some("Nami")));
    }

    #[pg_test]
    fn test_gets_name_field_default() {
        let retval = Spi::get_one::<&str>(
            "
            SELECT gets_name_field_default()
        ",
        );
        assert_eq!(retval, Ok(Some("Nami")));
    }

    #[pg_test]
    fn test_gets_name_field_strict() {
        let retval = Spi::get_one::<&str>(
            "
            SELECT gets_name_field_strict(ROW('Nami', 0)::Dog)
        ",
        );
        assert_eq!(retval, Ok(Some("Nami")));
    }

    #[pg_test]
    fn test_gets_name_field_variadic() {
        let retval = Spi::get_one::<Vec<String>>(
            "
            SELECT gets_name_field_variadic(ROW('Nami', 1)::Dog, ROW('Brandy', 1)::Dog)
        ",
        );
        assert_eq!(retval, Ok(Some(vec!["Nami".to_string(), "Brandy".to_string()])));
    }

    // #[pg_test]
    // fn test_sum_scritches_for_names() {
    //     let retval = Spi::get_one::<i32>(
    //         "
    //         SELECT sum_scritches_for_names(ARRAY[ROW('Nami', 1), ROW('Brandy', 42)]::Dog[])
    //     ",
    //     );
    //     assert_eq!(retval, Ok(Some(43)));
    // }

    // #[pg_test]
    // fn test_sum_scritches_for_names_default() {
    //     let retval = Spi::get_one::<i32>(
    //         "
    //         SELECT sum_scritches_for_names_default()
    //     ",
    //     );
    //     assert_eq!(retval, Ok(Some(0)));
    // }

    // #[pg_test]
    // fn test_sum_scritches_for_names_strict() {
    //     let retval = Spi::get_one::<i32>(
    //         "
    //         SELECT sum_scritches_for_names_strict(ARRAY[ROW('Nami', 1), ROW('Brandy', 42)]::Dog[])
    //     ",
    //     );
    //     assert_eq!(retval, Ok(Some(43)));
    // }

    // #[pg_test]
    // fn test_sum_scritches_for_names_strict_optional_items() {
    //     let retval = Spi::get_one::<i32>(
    //         "
    //         SELECT sum_scritches_for_names_strict(ARRAY[ROW('Nami', 1), ROW('Brandy', 42)]::Dog[])
    //     ",
    //     );
    //     assert_eq!(retval, Ok(Some(43)));
    // }

    // #[pg_test]
    // fn test_sum_scritches_for_names_default_optional_items() {
    //     let retval = Spi::get_one::<i32>(
    //         "
    //         SELECT sum_scritches_for_names_default_optional_items()
    //     ",
    //     );
    //     assert_eq!(retval, Ok(Some(0)));
    // }

    // #[pg_test]
    // fn test_sum_scritches_for_names_optional_items() {
    //     let retval = Spi::get_one::<i32>("
    //         SELECT sum_scritches_for_names_optional_items(ARRAY[ROW('Nami', 1), ROW('Brandy', 42)]::Dog[])
    //     ");
    //     assert_eq!(retval, Ok(Some(43)));
    // }

    // #[pg_test]
    // fn test_sum_scritches_for_names_array() {
    //     let retval = Spi::get_one::<i32>(
    //         "
    //         SELECT sum_scritches_for_names_array(ARRAY[ROW('Nami', 1), ROW('Brandy', 42)]::Dog[])
    //     ",
    //     );
    //     assert_eq!(retval, Ok(Some(43)));
    // }

    // #[pg_test]
    // fn test_sum_scritches_for_names_array_default() {
    //     let retval = Spi::get_one::<i32>(
    //         "
    //         SELECT sum_scritches_for_names_array_default()
    //     ",
    //     );
    //     assert_eq!(retval, Ok(Some(0)));
    // }

    // #[pg_test]
    // fn test_sum_scritches_for_names_array_strict() {
    //     let retval = Spi::get_one::<i32>("
    //         SELECT sum_scritches_for_names_array_strict(ARRAY[ROW('Nami', 1), ROW('Brandy', 42)]::Dog[])
    //     ");
    //     assert_eq!(retval, Ok(Some(43)));
    // }

    #[pg_test]
    fn test_create_dog() -> Result<(), pgrx::spi::Error> {
        let retval = Spi::get_one::<PgHeapTuple<'_, AllocatedByRust>>(
            "
            SELECT create_dog('Nami', 1)
        ",
        )?
        .expect("datum was null");
        assert_eq!(retval.get_by_name("name").unwrap(), Some("Nami"));
        assert_eq!(retval.get_by_name("scritches").unwrap(), Some(1));
        Ok(())
    }

    #[pg_test]
    fn test_scritch() -> Result<(), pgrx::spi::Error> {
        let retval = Spi::get_one::<PgHeapTuple<'_, AllocatedByRust>>(
            "
            SELECT scritch(ROW('Nami', 2)::Dog)
        ",
        )?
        .expect("datum was null");
        assert_eq!(retval.get_by_name("name").unwrap(), Some("Nami"));
        assert_eq!(retval.get_by_name("scritches").unwrap(), Some(2));
        Ok(())
    }

    #[pg_test]
    fn test_scritch_strict() -> Result<(), pgrx::spi::Error> {
        let retval = Spi::get_one::<PgHeapTuple<'_, AllocatedByRust>>(
            "
            SELECT scritch_strict(ROW('Nami', 2)::Dog)
        ",
        )?
        .expect("datum was null");
        assert_eq!(retval.get_by_name("name").unwrap(), Some("Nami"));
        assert_eq!(retval.get_by_name("scritches").unwrap(), Some(2));
        Ok(())
    }

    #[pg_test]
    fn test_new_composite_type() {
        Spi::run("CREATE TYPE DogWithAge AS (name text, age int);").expect("SPI failed");
        let mut heap_tuple = PgHeapTuple::new_composite_type("DogWithAge").unwrap();

        assert_eq!(heap_tuple.get_by_name::<String>("name").unwrap(), None);
        assert_eq!(heap_tuple.get_by_name::<i32>("age").unwrap(), None);

        heap_tuple.set_by_name("name", "Brandy".to_string()).unwrap();
        heap_tuple.set_by_name("age", 42).unwrap();

        assert_eq!(heap_tuple.get_by_name("name").unwrap(), Some("Brandy".to_string()));
        assert_eq!(heap_tuple.get_by_name("age").unwrap(), Some(42i32));
    }

    #[pg_test]
    fn test_missing_type() {
        const NON_EXISTING_ATTRIBUTE: &str = "DEFINITELY_NOT_EXISTING";

        match PgHeapTuple::new_composite_type(NON_EXISTING_ATTRIBUTE) {
            Err(PgHeapTupleError::NoSuchType(not_found))
                if not_found == NON_EXISTING_ATTRIBUTE.to_string() =>
            {
                ()
            }
            Err(err) => panic!("{err}"),
            Ok(_) => panic!("Able to find what should be a not existing composite type"),
        }
    }

    #[pg_test]
    fn test_missing_field() {
        Spi::run("CREATE TYPE DogWithAge AS (name text, age int);").expect("SPI failed");
        let mut heap_tuple = PgHeapTuple::new_composite_type("DogWithAge").unwrap();

        const NON_EXISTING_ATTRIBUTE: &str = "DEFINITELY_NOT_EXISTING";
        assert_eq!(
            heap_tuple.get_by_name::<String>(NON_EXISTING_ATTRIBUTE),
            Err(TryFromDatumError::NoSuchAttributeName(NON_EXISTING_ATTRIBUTE.into())),
        );

        assert_eq!(
            heap_tuple.set_by_name(NON_EXISTING_ATTRIBUTE, "Brandy".to_string()),
            Err(TryFromDatumError::NoSuchAttributeName(NON_EXISTING_ATTRIBUTE.into())),
        );
    }

    #[pg_test]
    fn test_missing_number() {
        Spi::run("CREATE TYPE DogWithAge AS (name text, age int);").expect("SPI failed");
        let mut heap_tuple = PgHeapTuple::new_composite_type("DogWithAge").unwrap();

        const NON_EXISTING_ATTRIBUTE: NonZeroUsize = unsafe { NonZeroUsize::new_unchecked(9001) };
        assert_eq!(
            heap_tuple.get_by_index::<String>(NON_EXISTING_ATTRIBUTE),
            Err(TryFromDatumError::NoSuchAttributeNumber(NON_EXISTING_ATTRIBUTE)),
        );

        assert_eq!(
            heap_tuple.set_by_index(NON_EXISTING_ATTRIBUTE, "Brandy".to_string()),
            Err(TryFromDatumError::NoSuchAttributeNumber(NON_EXISTING_ATTRIBUTE)),
        );
    }

    #[pg_test]
    fn test_wrong_type_assumed() {
        Spi::run("CREATE TYPE DogWithAge AS (name text, age int);").expect("SPI failed");
        let mut heap_tuple = PgHeapTuple::new_composite_type("DogWithAge").unwrap();

        // These are **deliberately** the wrong types.
        assert_eq!(
            heap_tuple.get_by_name::<i32>("name"),
            Ok(None), // We don't get an error here, yet...
        );
        assert_eq!(
            heap_tuple.get_by_name::<String>("age"),
            Ok(None), // We don't get an error here, yet...
        );

        // These are **deliberately** the wrong types.
        assert!(matches!(
            heap_tuple.set_by_name("name", 1_i32),
            Err(TryFromDatumError::IncompatibleTypes { .. })
        ));
        assert!(matches!(
            heap_tuple.set_by_name("age", "Brandy"),
            Err(TryFromDatumError::IncompatibleTypes { .. }),
        ));

        // Now set them properly, to test that we get errors when they're set...
        heap_tuple.set_by_name("name", "Brandy".to_string()).unwrap();
        heap_tuple.set_by_name("age", 42).unwrap();

        // These are **deliberately** the wrong types.
        assert!(matches!(
            heap_tuple.get_by_name::<i32>("name"),
            Err(TryFromDatumError::IncompatibleTypes { .. }),
        ));
        assert!(matches!(
            heap_tuple.get_by_name::<String>("age"),
            Err(TryFromDatumError::IncompatibleTypes { .. }),
        ));
    }

    #[pg_test]
    fn test_compatibility() {
        Spi::get_one::<PgHeapTuple<'_, AllocatedByRust>>("SELECT ROW('Nami', 2)::Dog")
            .expect("failed to get a Dog");

        // Non-composite types are incompatible:
        let not_a_dog = Spi::get_one::<PgHeapTuple<'_, AllocatedByRust>>("SELECT 1");
        match not_a_dog {
            Ok(_dog) => panic!("got a Dog when we shouldn't have"),
            Err(e) => {
                assert!(matches!(
                    e,
                    pgrx::spi::Error::DatumError(TryFromDatumError::IncompatibleTypes { .. })
                ))
            }
        }
    }

    #[pg_test]
    fn test_tuple_desc_clone() -> Result<(), spi::Error> {
        let result = Spi::connect(|client| {
            let query = "select * from generate_lots_of_dogs()";
            client.select(query, None, &[]).map(|table| table.len())
        })?;
        assert_eq!(result, 10_000);
        Ok(())
    }
}
