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
import static org.yb.AssertionWrappers.assertNotNull;
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
 * Tests for the DocumentDB Gateway using the MongoDB Java driver (auth disabled).
 *
 * The DocumentDB Gateway translates MongoDB wire protocol to PostgreSQL,
 * allowing standard MongoDB clients to connect to YugabyteDB.
 */
@RunWith(value = YBTestRunnerNonSanOrAArch64Mac.class)
public class TestDocumentDBGateway extends BaseDocumentDBGatewayTest {

  private static final String TEST_COLLECTION = "testcollection";

  @Test
  public void testInsertOne() throws Exception {
    MongoDatabase db = mongoClient.getDatabase(TEST_DB);
    MongoCollection<Document> collection = db.getCollection(TEST_COLLECTION);

    Document doc = new Document("name", "Alice")
        .append("age", 30)
        .append("city", "New York");

    InsertOneResult result = collection.insertOne(doc);
    assertNotNull(result.getInsertedId());

    Document found = collection.find(Filters.eq("name", "Alice")).first();
    assertNotNull(found);
    assertEquals("Alice", found.getString("name"));
    assertEquals(30, found.getInteger("age").intValue());
    assertEquals("New York", found.getString("city"));
  }

  @Test
  public void testInsertMany() throws Exception {
    MongoDatabase db = mongoClient.getDatabase(TEST_DB);
    MongoCollection<Document> collection = db.getCollection("patients");

    List<Document> docs = Arrays.asList(
        new Document("patient_id", "P001").append("name", "Alice Smith")
            .append("age", 30).append("conditions", Arrays.asList("Diabetes", "Hypertension")),
        new Document("patient_id", "P002").append("name", "Bob Johnson")
            .append("age", 45).append("conditions", Arrays.asList("Asthma")),
        new Document("patient_id", "P003").append("name", "Charlie Brown")
            .append("age", 29).append("conditions", Arrays.asList("Allergy", "Anemia")),
        new Document("patient_id", "P004").append("name", "Diana Prince")
            .append("age", 40).append("conditions", Arrays.asList("Migraine")),
        new Document("patient_id", "P005").append("name", "Edward Norton")
            .append("age", 55).append("conditions",
                Arrays.asList("Hypertension", "Heart Disease"))
    );

    InsertManyResult result = collection.insertMany(docs);
    assertEquals(5, result.getInsertedIds().size());

    long count = collection.countDocuments();
    assertEquals(5, count);
  }

  @Test
  public void testFind() throws Exception {
    MongoDatabase db = mongoClient.getDatabase(TEST_DB);
    MongoCollection<Document> collection = db.getCollection("findtest");

    // Insert test data.
    List<Document> docs = Arrays.asList(
        new Document("type", "fruit").append("name", "apple").append("qty", 10),
        new Document("type", "fruit").append("name", "banana").append("qty", 20),
        new Document("type", "vegetable").append("name", "carrot").append("qty", 15),
        new Document("type", "fruit").append("name", "cherry").append("qty", 5)
    );
    collection.insertMany(docs);

    // Find all fruits.
    List<Document> fruits = new ArrayList<>();
    collection.find(Filters.eq("type", "fruit")).into(fruits);
    assertEquals(3, fruits.size());

    // Find with greater-than filter.
    List<Document> highQty = new ArrayList<>();
    collection.find(Filters.gt("qty", 10)).into(highQty);
    assertEquals(2, highQty.size());

    // Find with AND filter.
    Document fruitHighQty = collection.find(
        Filters.and(Filters.eq("type", "fruit"), Filters.gte("qty", 20))).first();
    assertNotNull(fruitHighQty);
    assertEquals("banana", fruitHighQty.getString("name"));
  }

  @Test
  public void testUpdateOne() throws Exception {
    MongoDatabase db = mongoClient.getDatabase(TEST_DB);
    MongoCollection<Document> collection = db.getCollection("updatetest");

    collection.insertOne(new Document("name", "Alice").append("age", 30));

    UpdateResult result = collection.updateOne(
        Filters.eq("name", "Alice"),
        Updates.set("age", 31));

    assertEquals(1, result.getMatchedCount());
    assertEquals(1, result.getModifiedCount());

    Document updated = collection.find(Filters.eq("name", "Alice")).first();
    assertNotNull(updated);
    assertEquals(31, updated.getInteger("age").intValue());
  }

  @Test
  public void testUpdateMany() throws Exception {
    MongoDatabase db = mongoClient.getDatabase(TEST_DB);
    MongoCollection<Document> collection = db.getCollection("updatemanytest");

    List<Document> docs = Arrays.asList(
        new Document("status", "active").append("score", 10),
        new Document("status", "active").append("score", 20),
        new Document("status", "inactive").append("score", 30)
    );
    collection.insertMany(docs);

    UpdateResult result = collection.updateMany(
        Filters.eq("status", "active"),
        Updates.set("score", 99));

    assertEquals(2, result.getMatchedCount());
    assertEquals(2, result.getModifiedCount());

    // Verify the inactive one was not updated.
    Document inactive = collection.find(Filters.eq("status", "inactive")).first();
    assertNotNull(inactive);
    assertEquals(30, inactive.getInteger("score").intValue());
  }

  @Test
  public void testDeleteOne() throws Exception {
    MongoDatabase db = mongoClient.getDatabase(TEST_DB);
    MongoCollection<Document> collection = db.getCollection("deletetest");

    collection.insertMany(Arrays.asList(
        new Document("name", "Alice"),
        new Document("name", "Bob"),
        new Document("name", "Charlie")
    ));

    DeleteResult result = collection.deleteOne(Filters.eq("name", "Bob"));
    assertEquals(1, result.getDeletedCount());

    long count = collection.countDocuments();
    assertEquals(2, count);
  }

  @Test
  public void testDeleteMany() throws Exception {
    MongoDatabase db = mongoClient.getDatabase(TEST_DB);
    MongoCollection<Document> collection = db.getCollection("deletemanytest");

    collection.insertMany(Arrays.asList(
        new Document("category", "A").append("value", 1),
        new Document("category", "A").append("value", 2),
        new Document("category", "B").append("value", 3),
        new Document("category", "A").append("value", 4)
    ));

    DeleteResult result = collection.deleteMany(Filters.eq("category", "A"));
    assertEquals(3, result.getDeletedCount());

    long count = collection.countDocuments();
    assertEquals(1, count);
  }

  @Test
  public void testNestedDocuments() throws Exception {
    MongoDatabase db = mongoClient.getDatabase(TEST_DB);
    MongoCollection<Document> collection = db.getCollection("nestedtest");

    Document doc = new Document("name", "Alice")
        .append("address", new Document("street", "123 Main St")
            .append("city", "Springfield")
            .append("state", "IL"))
        .append("scores", Arrays.asList(85, 92, 78));

    collection.insertOne(doc);

    // Query nested document field.
    Document found = collection.find(Filters.eq("address.city", "Springfield")).first();
    assertNotNull(found);
    assertEquals("Alice", found.getString("name"));

    Document address = found.get("address", Document.class);
    assertNotNull(address);
    assertEquals("123 Main St", address.getString("street"));
    assertEquals("IL", address.getString("state"));
  }

  @Test
  public void testCountDocuments() throws Exception {
    MongoDatabase db = mongoClient.getDatabase(TEST_DB);
    MongoCollection<Document> collection = db.getCollection("counttest");

    assertEquals(0, collection.countDocuments());

    List<Document> docs = new ArrayList<>();
    for (int i = 0; i < 10; i++) {
      docs.add(new Document("index", i).append("even", i % 2 == 0));
    }
    collection.insertMany(docs);

    assertEquals(10, collection.countDocuments());
    assertEquals(5, collection.countDocuments(Filters.eq("even", true)));
    assertEquals(5, collection.countDocuments(Filters.eq("even", false)));
  }

  @Test
  public void testDistinct() throws Exception {
    MongoDatabase db = mongoClient.getDatabase(TEST_DB);
    MongoCollection<Document> collection = db.getCollection("distincttest");

    collection.insertMany(Arrays.asList(
        new Document("dept", "engineering").append("level", "senior"),
        new Document("dept", "engineering").append("level", "junior"),
        new Document("dept", "marketing").append("level", "senior"),
        new Document("dept", "engineering").append("level", "senior"),
        new Document("dept", "sales").append("level", "junior")
    ));

    List<String> departments = new ArrayList<>();
    collection.distinct("dept", String.class).into(departments);
    assertEquals(3, departments.size());
    assertTrue(departments.contains("engineering"));
    assertTrue(departments.contains("marketing"));
    assertTrue(departments.contains("sales"));
  }

  @Test
  public void testReplaceOne() throws Exception {
    MongoDatabase db = mongoClient.getDatabase(TEST_DB);
    MongoCollection<Document> collection = db.getCollection("replacetest");

    collection.insertOne(new Document("name", "Alice").append("age", 30).append("city", "NYC"));

    UpdateResult result = collection.replaceOne(
        Filters.eq("name", "Alice"),
        new Document("name", "Alice").append("age", 31).append("city", "Boston")
            .append("status", "relocated"));

    assertEquals(1, result.getMatchedCount());
    assertEquals(1, result.getModifiedCount());

    Document replaced = collection.find(Filters.eq("name", "Alice")).first();
    assertNotNull(replaced);
    assertEquals(31, replaced.getInteger("age").intValue());
    assertEquals("Boston", replaced.getString("city"));
    assertEquals("relocated", replaced.getString("status"));
  }
}
