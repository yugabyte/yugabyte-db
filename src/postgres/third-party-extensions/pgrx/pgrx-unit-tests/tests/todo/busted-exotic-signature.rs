use pgrx::prelude::*;

fn main() {}

// Just a compile test...
// We don't run these, but we ensure we can build SQL for them
mod sql_generator_tests {
    use super::*;

    #[pg_extern]
    fn exotic_signature(
        _cats: pgrx::default!(
            Option<Vec<Option<::pgrx::composite_type!('static, "Cat")>>>,
            "ARRAY[ROW('Sally', 0)]::Cat[]"
        ),
        _a_single_fish: pgrx::default!(
            Option<::pgrx::composite_type!('static, "Fish")>,
            "ROW('Bob', 0)::Fish"
        ),
        _dogs: pgrx::default!(
            Option<::pgrx::VariadicArray<::pgrx::composite_type!('static, "Dog")>>,
            "ARRAY[ROW('Nami', 0), ROW('Brandy', 0)]::Dog[]"
        ),
    ) -> TableIterator<
        'static,
        (
            name!(dog, Option<::pgrx::composite_type!('static, "Dog")>),
            name!(cat, Option<::pgrx::composite_type!('static, "Cat")>),
            name!(fish, Option<::pgrx::composite_type!('static, "Fish")>),
            name!(
                related_edges,
                Option<Vec<::pgrx::composite_type!('static, "AnimalFriendshipEdge")>>
            ),
        ),
    > {
        TableIterator::new(Vec::new().into_iter())
    }
}
