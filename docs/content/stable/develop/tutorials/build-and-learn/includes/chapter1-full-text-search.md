```text
space travel
```

The service will use the prompt to perform a full-text search across movie overviews. The results will appear as follows:

![Full-Text Search Result](/images/tutorials/build-and-learn/chapter1-full-text-search-result.png)

PostgreSQL can filter movies by rank and category before doing the full-text search. For instance, set rank to **7**, choose **Science Fiction** as the category, and repeat the search again:

![Full-Text Search With Pre-Filtering Result](/images/tutorials/build-and-learn/chapter1-full-text-search-pre-filtering.png)

Here's the SQL query that YugaPlus uses to find the movie recommendations:

```sql
SELECT id, title, overview, vote_average, release_date FROM movie 
WHERE vote_average >= :rank 
AND genres @> :category::jsonb 
AND overview_lexemes @@ plainto_tsquery('english', :prompt) 
LIMIT :max_results
```
