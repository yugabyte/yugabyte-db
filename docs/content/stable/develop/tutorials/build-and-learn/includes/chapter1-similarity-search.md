```text
I'd like to watch a movie about a space adventure
```

The service turns your prompt into an embedding with OpenAI's model, then searches for similar movie descriptions. The results should look like this:

![Similarity Search Result](/images/tutorials/build-and-learn/chapter1-similarity-search-result.png)

PostgreSQL can filter movies by rank and category before doing the vector search. For instance, set rank to **7**, choose **Science Fiction** as the category, and repeat the search again:

![Similarity Search With Pre-Filtering Result](/images/tutorials/build-and-learn/chapter1-similarity-search-pre-filtering.png)
*(Hint: Pick a movie you like and add it to your library with the **Add to Library** button.)*

Here's the SQL query that YugaPlus uses to find the movie recommendations:

```sql
SELECT id, title, overview, vote_average, release_date FROM movie 
WHERE vote_average >= :rank 
AND genres @> :category::jsonb 
AND 1 - (overview_vector <=> :prompt_vector::vector) >= :similarity_threshold 
ORDER BY overview_vector <=> :prompt_vector::vector 
LIMIT :max_results
```
