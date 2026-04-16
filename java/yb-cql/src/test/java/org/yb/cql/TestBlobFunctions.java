package org.yb.cql;

import org.junit.Test;

import java.util.*;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import static org.yb.AssertionWrappers.assertEquals;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestBlobFunctions extends BaseCQLTest {

    /**
     * Testing LOGIC:
     *
     * The Blob Function Tests checks the correctness of the TypeAsBlob and
     * its inverse functions and that it can be used as part of select
     * statements and insert statements. We also check for invalid statements.
     * We follow the same (well almost!) steps for testing each type.
     *
     * 1) We create a table where one column is a blob and the other
     * is the corresponding type. We also construct an auxiliary hash map
     * which stores a few values of a particular data type and its
     * corresponding Blob representation in Cassandra.
     *
     * 2) We insert rows into the table using TypeAsBlob(data)
     * into the blob column (say B), and BlobAsType(blob) (say D) in the data column
     * (where blob is the 'correct' representation of the data as a blob in Cassandra).
     *
     * 3) We check that the return values TypeAsBlob(D) is exactly equal to the
     * blob values we have in the hashmap. This tests that TypeAsBlob is the inverse of
     * BlobAsType.
     *
     * 4) We check that the return values of BlobAsType(B) is exactly equal to
     * the data values we have in the hashmap. This tests that BlobAsType is the
     * inverse of TypeAsBlob. We skip this step for floats and doubles since they do
     * not have an invertible binary representation.
     *
     * 5) Finally we check that TypeAsBlob of the data values we have in the hashmap
     * is equal to the blob values we have in the hashmap. This tests that our TypeAsBlob
     * implementation is Cassandra compatible.
     *
     */

    protected void createTable(String type) throws Exception {
        session.execute("create table table_" + type +
                " (k int primary key, b blob, t " + type + ");");
    }

    void insertIntoTableStatement(int key, String type, String typeVal,
                                  String blobVal) throws Exception {
        session.execute("insert into table_" + type + " (k, b, t) values " +
                "("+ key + ", " + type + "asblob(" + typeVal + "), blobas"
                + type + "(" + blobVal + "));");
    }

    void insertIntoTableStatement(int key, String type, String typeVal,
                                      String blobVal, boolean isText) throws Exception {
        if (isText) {
            typeVal = '\'' + typeVal + '\'';
        }
        session.execute("insert into table_" + type + " (k, b, t) values " +
                "("+ key + ", " + type + "asblob(" + typeVal + "), blobas"
                + type + "(" + blobVal + "));");
    }

    void runInvalidStatement(int key, String type, String typeVal) {
        runInvalidStmt("insert into table_" + type + " (k, t) values (" + key + ", "
        + type + "asblob(" + typeVal + "));");
    }

    void assertBlobQuery(String stmt, Set<String> expectedBlobs) {
        ResultSet rs = session.execute(stmt);
        HashSet<String> actualBlobs = new HashSet<>();
        for (Row row : rs) {
            actualBlobs.add(makeBlobString(row.getBytes(0)));
        }
        assertEquals(expectedBlobs, actualBlobs);
    }

    HashSet<String> constructRowSet(Set<String> keys) {
        HashSet<String> rowSet = new HashSet<>();
        for (String key : keys) {
            rowSet.add("Row[" + key + "]");
        }
        return rowSet;
    }

    void testTypeAsBlobCorrectness(String type, Set<String> blobSet) {
        assertBlobQuery("select " + type + "asblob(t) from table_"
                + type + ";", blobSet);
        assertBlobQuery("select b from table_" + type + ";", blobSet);
    }

    void testBlobAsTypeCorrectness(String type, HashSet<String> rowSet) {
        assertQuery("select blobas" + type + "(b) from table_" + type + ";", rowSet);
    }

    Map<String, String> validDataBlobKV = new HashMap<String, String> ();

    @Test
    public void testBooleanToBlobConversions() throws Exception {
        // Creating Table
        createTable("boolean");

        // Constructing columns that must be inserted as a KV pair.
        validDataBlobKV.clear();
        validDataBlobKV.put("true", "0x01");
        validDataBlobKV.put("false", "0x00");

        // Inserting columns.
        int iter = 1;
        for (String key : validDataBlobKV.keySet()) {
            insertIntoTableStatement(iter++,"boolean", key, validDataBlobKV.get(key));
        }

        // Testing TypeAsBlob and BlobAsType correctness.
        testTypeAsBlobCorrectness("boolean", new HashSet<>(validDataBlobKV.values()));
        testBlobAsTypeCorrectness("boolean", constructRowSet(validDataBlobKV.keySet()));

        // Adding Invalid Boolean
        runInvalidStatement(iter++, "boolean", "int");
        // Adding Invalid Blobs
        runInvalidStatement(iter++, "boolean", "0x1101");
    }

    @Test
    public void testTinyIntToBlobConversions() throws Exception {
        // Creating Table
        createTable("tinyint");

        // Constructing columns that must be inserted as a KV pair.
        validDataBlobKV.clear();
        validDataBlobKV.put("121", "0x79");
        validDataBlobKV.put("-19", "0xed");
        validDataBlobKV.put("71", "0x47");

        // Inserting columns.
        int iter = 1;
        for (String key : validDataBlobKV.keySet()) {
            insertIntoTableStatement(iter++,"tinyint", key, validDataBlobKV.get(key));
        }

        // Testing TypeAsBlob and BlobAsType correctness.
        testTypeAsBlobCorrectness("tinyint", new HashSet<>(validDataBlobKV.values()));
        testBlobAsTypeCorrectness("tinyint", constructRowSet(validDataBlobKV.keySet()));

        // Adding Invalid Tinyints
        runInvalidStatement(iter++, "tinyint", "255");
        runInvalidStatement(iter++, "tinyint", "255");
        // Adding Invalid Blobs
        runInvalidStatement(iter++, "tinyint", "0x12392f");
    }

    @Test
    public void testSmallIntToBlobConversions() throws Exception {
        // Creating Table
        createTable("smallint");

        // Constructing columns that must be inserted as a KV Pair.
        validDataBlobKV.clear();
        validDataBlobKV.put("1992", "0x07c8");
        validDataBlobKV.put("-219", "0xff25");
        validDataBlobKV.put("9012", "0x2334");

        // Inserting columns.
        int iter = 1;
        for (String key : validDataBlobKV.keySet()) {
            insertIntoTableStatement(iter++,"smallint", key, validDataBlobKV.get(key));
        }

        // Testing TypeAsBlob and BlobAsType correctness.
        testTypeAsBlobCorrectness("smallint", new HashSet<>(validDataBlobKV.values()));
        testBlobAsTypeCorrectness("smallint", constructRowSet(validDataBlobKV.keySet()));

        // Adding Invalid Smallints
        runInvalidStatement(iter++, "smallint", "123123123");
        runInvalidStatement(iter++, "smallint", "-123123123");
        // Adding Invalid Blobs
        runInvalidStatement(iter++, "smallint", "0x12");
    }

    @Test
    public void testIntToBlobConversions() throws Exception {
        // Creating Table
        createTable("int");

        // Constructing columns that muct be inserted as a KV Pair.
        validDataBlobKV.clear();
        validDataBlobKV.put("1992123", "0x001e65bb");
        validDataBlobKV.put("-21962", "0xffffaa36");
        validDataBlobKV.put("-9012", "0xffffdccc");

        // Inserting columns.
        int iter = 1;
        for (String key : validDataBlobKV.keySet()) {
            insertIntoTableStatement(iter++,"int", key, validDataBlobKV.get(key));
        }

        // Testing TypeAsBlob and BlobAsType correctness.
        testTypeAsBlobCorrectness("int", new HashSet<>(validDataBlobKV.values()));
        testBlobAsTypeCorrectness("int", constructRowSet(validDataBlobKV.keySet()));

        // Adding Invalid Ints
        runInvalidStatement(iter++, "int", "12312312312123213122132");
        runInvalidStatement(iter++, "int", "-12312312312123213122132");
        // Adding Invalid Blobs
        runInvalidStatement(iter++, "int", "0x12");
        runInvalidStatement(iter++, "int", "0x001e65bb001e65bb");
    }

    @Test
    public void testBigIntToBlobConversions() throws Exception {
        // Creating Table
        createTable("bigint");

        // Constructing columns that must be inserted as a KV Pair.
        validDataBlobKV.clear();
        validDataBlobKV.put("1992123", "0x00000000001e65bb");
        validDataBlobKV.put("-912321962", "0xffffffffc99f1256");
        validDataBlobKV.put("-9012", "0xffffffffffffdccc");

        // Inserting columns.
        int iter = 1;
        for (String key : validDataBlobKV.keySet()) {
            insertIntoTableStatement(iter++,"bigint", key, validDataBlobKV.get(key));
        }

        // Testing TypeAsBlob and BlobAsType correctness.
        testTypeAsBlobCorrectness("bigint", new HashSet<>(validDataBlobKV.values()));
        testBlobAsTypeCorrectness("bigint", constructRowSet(validDataBlobKV.keySet()));

        // Adding Invalid BigInts
        runInvalidStatement(iter++, "bigint", "999999999999912312312312123213122132");
        runInvalidStatement(iter++, "bigint", "-999999999999912312312312123213122132");
        // Adding Invalid Blobs
        runInvalidStatement(iter++, "bigint", "0x12");
        runInvalidStatement(iter++, "bigint", "0x001e65bb001e65bb");
    }

    @Test
    public void testFloatToBlobConversions() throws Exception {
        // Creating Table
        createTable("float");

        // Constructing columns that must be inserted as a KV Pair.
        validDataBlobKV.clear();
        validDataBlobKV.put("17.31", "0x418a7ae1");
        validDataBlobKV.put("198.2", "0x43463333");
        validDataBlobKV.put("21199.08984", "0x46a59e2e");

        // Inserting columns.
        int iter = 1;
        for (String key : validDataBlobKV.keySet()) {
            insertIntoTableStatement(iter++,"float", key, validDataBlobKV.get(key));
        }

        // Testing Type As Blob Correctness
        testTypeAsBlobCorrectness("float", new HashSet<>(validDataBlobKV.values()));

        // Adding Invalid Floats
        runInvalidStatement(iter++, "float", "12312312312123213122132.99999999999999999999");
        runInvalidStatement(iter++, "float", "-12312312312123213122132.9999999999999999999");
        // Adding Invalid Blobs
        runInvalidStatement(iter++, "float", "0x12");
        runInvalidStatement(iter++, "float", "0x001e65bb001e65bb");
    }

    @Test
    public void testDoubleToBlobConversions() throws Exception {
        // Creating Table
        createTable("double");

        // Constructing columns that must be inserted as a KV Pair.
        validDataBlobKV.clear();
        validDataBlobKV.put("19213.223", "0x40d2c34e45a1cac1");
        validDataBlobKV.put("21199.09", "0x40d4b3c5c28f5c29");
        validDataBlobKV.put("98765.4321", "0x40f81cd6e9e1b08a");

        // Inserting columns.
        int iter = 1;
        for (String key : validDataBlobKV.keySet()) {
            insertIntoTableStatement(iter++,"double", key, validDataBlobKV.get(key));
        }

        // Testing Type As Blob Correctness
        testTypeAsBlobCorrectness("double", new HashSet<>(validDataBlobKV.values()));

        // Adding Invalid Doubles
        runInvalidStatement(iter++, "float",
                "999999999999912312312312123213122132.99999999999999999999");
        runInvalidStatement(iter++, "float",
                "-999999999999912312312312123213122132.99999999999999999999");
        // Adding Invalid Blobs
        runInvalidStatement(iter++, "float", "0x12");
        runInvalidStatement(iter++, "float", "0x001e65bb001e65bb001e65bb001e65bb");
    }

    @Test
    public void testUUIDToBlobConversions() throws Exception {
        // Creating Table
        createTable("uuid");

        // Constructing columns that must be inserted as a KV Pair.
        validDataBlobKV.clear();
        validDataBlobKV.put("fa713975-2630-4e37-aaef-465ab850b986",
                "0xfa71397526304e37aaef465ab850b986");
        validDataBlobKV.put("632c0b5a-fcbb-11e7-8be5-0ed5f89f718b",
                "0x632c0b5afcbb11e78be50ed5f89f718b");
        validDataBlobKV.put("0b813001-799a-47be-9c4b-1105f1d71def",
                "0x0b813001799a47be9c4b1105f1d71def");
        validDataBlobKV.put("799cb5a0-5810-11e8-9c2d-fa7ae01bbebc",
                "0x799cb5a0581011e89c2dfa7ae01bbebc");

        // Inserting columns.
        int iter = 1;
        for (String key : validDataBlobKV.keySet()) {
            insertIntoTableStatement(iter++,"uuid", key, validDataBlobKV.get(key));
        }

        // Testing TypeAsBlob and BlobAsType correctness.
        testTypeAsBlobCorrectness("uuid", new HashSet<>(validDataBlobKV.values()));
        testBlobAsTypeCorrectness("uuid", constructRowSet(validDataBlobKV.keySet()));

        // Adding Invalid UUIDs
        runInvalidStatement(iter++, "uuid", "ab2-2ebe1b0e-3efb-4b17-8553-fe668d605dbb-cd2");
        // Adding Invalid Blobs
        runInvalidStatement(iter++, "uuid", "0x632c0b5afcbb11e78be50ed5f8");
        runInvalidStatement(iter++, "uuid", "0x632c0b5afcbb11e750ed5f8");
    }

    @Test
    public void testTimeUUIDToBlobConversions() throws Exception {
        // Creating Table
        createTable("timeuuid");

        // Constructing columns that must be inserted as a KV Pair.
        validDataBlobKV.clear();
        validDataBlobKV.put("799cb5a0-5810-11e8-9c2d-fa7ae01bbebc",
                "0x799cb5a0581011e89c2dfa7ae01bbebc");
        validDataBlobKV.put("632c0b5a-fcbb-11e7-8be5-0ed5f89f718b",
                "0x632c0b5afcbb11e78be50ed5f89f718b");

        // Inserting columns.
        int iter = 1;
        for (String key : validDataBlobKV.keySet()) {
            insertIntoTableStatement(iter++,"timeuuid", key, validDataBlobKV.get(key));
        }

        // Testing TypeAsBlob and BlobAsType correctness.
        testTypeAsBlobCorrectness("timeuuid", new HashSet<>(validDataBlobKV.values()));
        testBlobAsTypeCorrectness("timeuuid", constructRowSet(validDataBlobKV.keySet()));

        // Adding Invalid TimeUUIDs
        runInvalidStatement(iter++, "timeuuid", "ab2-2ebe1b0e-3efb-4b17-8553-fe668d605dbb-cd2");
        runInvalidStatement(iter++, "timeuuid", "4ffd6119-2926-47f3-bacf-aa02128d6eec");
        // Adding Invalid Blobs
        runInvalidStatement(iter++, "timeuuid", "0x632c0b5afcbb11e78be50ed5f8");
        runInvalidStatement(iter++, "timeuuid", "0x632c0b5afcbb11e750ed5f8");
        runInvalidStatement(iter++, "timeuuid", "0x4ffd6119292647f3bacfaa02128d6eec");
    }

    @Test
    public void testTextToBlobConversions() throws Exception {
        // Creating Table
        createTable("text");

        // Constructing columns that must be inserted as a KV Pair.
        validDataBlobKV.clear();
        validDataBlobKV.put("abcd", "0x61626364");
        validDataBlobKV.put("Hello World!", "0x48656c6c6f20576f726c6421");
        validDataBlobKV.put("a man a plan a canal panama",
                "0x61206d616e206120706c616e20612063616e616c2070616e616d61");

        // Inserting columns.
        int iter = 1;
        for (String key : validDataBlobKV.keySet()) {
            insertIntoTableStatement(iter++,"text", key, validDataBlobKV.get(key), true);
        }

        // Testing TypeAsBlob and BlobAsType correctness.
        testTypeAsBlobCorrectness("text", new HashSet<>(validDataBlobKV.values()));
        testBlobAsTypeCorrectness("text", constructRowSet(validDataBlobKV.keySet()));

        // Skipping invalid data statements since any text is a valid entry.
        // A blob having odd number of hex digits cannot be a valid text.
        runInvalidStatement(iter++, "text", "0x61626364f");
    }

    @Test
    public void testVarcharToBlobConversions() throws Exception {
        // Creating Table
        createTable("varchar");

        // Constructing columns that must be inserted as a KV Pair.
        validDataBlobKV.clear();
        validDataBlobKV.put("abcd", "0x61626364");
        validDataBlobKV.put("Hello World!", "0x48656c6c6f20576f726c6421");
        validDataBlobKV.put("a man a plan a canal panama",
                "0x61206d616e206120706c616e20612063616e616c2070616e616d61");

        // Inserting columns.
        int iter = 1;
        for (String key : validDataBlobKV.keySet()) {
            insertIntoTableStatement(iter++,"varchar", key, validDataBlobKV.get(key), true);
        }

        // Testing TypeAsBlob and BlobAsType correctness.
        testTypeAsBlobCorrectness("varchar", new HashSet<>(validDataBlobKV.values()));
        testBlobAsTypeCorrectness("varchar", constructRowSet(validDataBlobKV.keySet()));

        // Skipping invalid data statements since any varchar is a valid entry.
        // A blob having odd number of hex digits cannot be a valid varchar.
        runInvalidStatement(iter++, "varchar", "0x61626364f");
    }

    @Test
    public void testTimestampToBlobConversions() throws Exception {
        // Creating Table
        createTable("timestamp");

        // Constructing columns that must be inserted as a KV Pair.
        validDataBlobKV.clear();
        validDataBlobKV.put("1516326741100", "0x000001610c1de46c");
        validDataBlobKV.put("1526511596217", "0x000001636b2e72b9");
        validDataBlobKV.put("1526511450685", "0x000001636b2c3a3d");
        validDataBlobKV.put("123123213", "0x000000000756b60d");

        // Inserting columns.
        int iter = 1;
        for (String key : validDataBlobKV.keySet()) {
            insertIntoTableStatement(iter++,"timestamp", key, validDataBlobKV.get(key));
        }

        // Testing TypeAsBlob correctness.
        testTypeAsBlobCorrectness("timestamp", new HashSet<>(validDataBlobKV.values()));

        // Adding Invalid Timestamps
        runInvalidStatement(iter++, "timestamp", "999999999999912312312312123213122132");
        runInvalidStatement(iter++, "timestamp", "-999999999999912312312312123213122132");
        // Adding Invalid Blobs
        runInvalidStatement(iter++, "timestamp", "0x12");
        runInvalidStatement(iter++, "timestamp", "0x001e65bb001e65bb");
    }
}
