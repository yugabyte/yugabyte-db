// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;

import com.mongodb.client.MongoCollection;
import com.mongodb.client.MongoDatabase;
import com.mongodb.client.model.Filters;
import com.mongodb.client.model.Updates;
import com.mongodb.client.result.DeleteResult;
import com.mongodb.client.result.InsertManyResult;
import com.mongodb.client.result.InsertOneResult;
import com.mongodb.client.result.UpdateResult;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import org.bson.Document;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.YBTestRunnerNonSanOrAArch64Mac;

/**
 * End-to-end test for the DocumentDB Gateway using the MongoDB Java driver.
 *
 * Covers insert, find, update, delete, nested documents, count, distinct,
 * replace, drop collection, and drop database in a single scenario.
 */
@RunWith(value = YBTestRunnerNonSanOrAArch64Mac.class)
public class BasicDocumentDBTests extends BaseDocumentDBGatewayTest {

  @Test
  public void testBasicOperations() throws Exception {
    MongoDatabase db = mongoClient.getDatabase(TEST_DB);
    String collName = "testcollection";
    MongoCollection<Document> collection = db.getCollection(collName);

    // --- insertOne ---
    Document doc = new Document("name", "Alice")
        .append("age", 30)
        .append("city", "New York");
    InsertOneResult insertOneResult = collection.insertOne(doc);
    assertNotNull(insertOneResult.getInsertedId());

    Document found = collection.find(Filters.eq("name", "Alice")).first();
    assertNotNull(found);
    assertEquals("Alice", found.getString("name"));
    assertEquals(30, found.getInteger("age").intValue());
    assertEquals("New York", found.getString("city"));

    // --- insertMany ---
    List<Document> docs = Arrays.asList(
        new Document("name", "Bob").append("age", 45).append("city", "Chicago"),
        new Document("name", "Charlie").append("age", 29).append("city", "Boston"),
        new Document("name", "Diana").append("age", 40).append("city", "Seattle"),
        new Document("name", "Edward").append("age", 55).append("city", "Denver")
    );
    InsertManyResult insertManyResult = collection.insertMany(docs);
    assertEquals(4, insertManyResult.getInsertedIds().size());
    assertEquals(5, collection.countDocuments());

    // --- find with filters ---
    List<Document> results = new ArrayList<>();
    collection.find(Filters.gt("age", 40)).into(results);
    assertEquals(2, results.size());

    Document andResult = collection.find(
        Filters.and(Filters.gte("age", 40), Filters.eq("city", "Seattle"))).first();
    assertNotNull(andResult);
    assertEquals("Diana", andResult.getString("name"));

    // --- updateOne ---
    UpdateResult updateOneResult = collection.updateOne(
        Filters.eq("name", "Alice"),
        Updates.set("age", 31));
    assertEquals(1, updateOneResult.getMatchedCount());
    assertEquals(1, updateOneResult.getModifiedCount());
    Document updated = collection.find(Filters.eq("name", "Alice")).first();
    assertEquals(31, updated.getInteger("age").intValue());

    // --- updateMany ---
    UpdateResult updateManyResult = collection.updateMany(
        Filters.gt("age", 40),
        Updates.set("status", "senior"));
    assertEquals(2, updateManyResult.getMatchedCount());
    assertEquals(2, updateManyResult.getModifiedCount());

    // --- replaceOne ---
    UpdateResult replaceResult = collection.replaceOne(
        Filters.eq("name", "Charlie"),
        new Document("name", "Charlie").append("age", 30).append("city", "Portland")
            .append("relocated", true));
    assertEquals(1, replaceResult.getMatchedCount());
    Document replaced = collection.find(Filters.eq("name", "Charlie")).first();
    assertEquals("Portland", replaced.getString("city"));
    assertTrue(replaced.getBoolean("relocated"));

    // --- nested documents ---
    collection.insertOne(new Document("name", "Frank")
        .append("address", new Document("street", "123 Main St")
            .append("city", "Springfield")
            .append("state", "IL"))
        .append("scores", Arrays.asList(85, 92, 78)));
    Document nested = collection.find(Filters.eq("address.city", "Springfield")).first();
    assertNotNull(nested);
    assertEquals("Frank", nested.getString("name"));
    Document address = nested.get("address", Document.class);
    assertEquals("IL", address.getString("state"));

    // --- countDocuments with filter ---
    assertEquals(6, collection.countDocuments());
    assertEquals(2, collection.countDocuments(Filters.eq("status", "senior")));

    // --- distinct ---
    // 5 documents have a top-level city field, each unique: New York, Chicago,
    // Portland (was Boston, overwritten by replaceOne), Seattle, Denver. Frank's
    // city is nested under address.city, so doesn't contribute.
    List<String> cities = new ArrayList<>();
    collection.distinct("city", String.class).into(cities);
    assertEquals(5, cities.size());
    assertTrue(cities.contains("Portland"));

    // --- deleteOne ---
    DeleteResult deleteOneResult = collection.deleteOne(Filters.eq("name", "Frank"));
    assertEquals(1, deleteOneResult.getDeletedCount());
    assertEquals(5, collection.countDocuments());

    // --- deleteMany ---
    DeleteResult deleteManyResult = collection.deleteMany(Filters.eq("status", "senior"));
    assertEquals(2, deleteManyResult.getDeletedCount());
    assertEquals(3, collection.countDocuments());

    // --- drop collection and validate ---
    collection.drop();
    // After dropping, the collection should have 0 documents when re-accessed.
    MongoCollection<Document> droppedColl = db.getCollection(collName);
    assertEquals(0, droppedColl.countDocuments());

    // --- drop database and validate ---
    db.drop();
    // After dropping, a new reference to the database should show no collections with data.
    MongoDatabase droppedDb = mongoClient.getDatabase(TEST_DB);
    MongoCollection<Document> afterDrop = droppedDb.getCollection(collName);
    assertEquals(0, afterDrop.countDocuments());
  }
}
