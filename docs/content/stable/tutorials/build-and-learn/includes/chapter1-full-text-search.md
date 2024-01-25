```output
adventure
```

The service will use the prompt to perform a full-text search over the movies overviews. The result can be as follows:

TBD: picture

PostgreSQL can also pre-filter the movies based on the rank and category before performing the full-text search.

For instance, set the **rank** to `7`, select the **Science Fiction** category and repeat the search. The suggestions should be similar to the ones below. Pick the one you like and to your own library by clicking the **Add to Library** button.

![Similarity Search Result](/images/tutorials/build-and-learn/chapter1-similarity-search-pre-filtering.png)

The actual SQL query used to generate movie recommendations is as follows:

```sql
SELECT id, title, overview, vote_average, release_date FROM movie 
WHERE vote_average >= :rank 
AND genres @> :category::jsonb 
AND overview LIKE :prompt 
LIMIT :max_results
```
