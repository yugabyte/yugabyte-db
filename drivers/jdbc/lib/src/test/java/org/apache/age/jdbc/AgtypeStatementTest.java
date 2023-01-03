/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package org.apache.age.jdbc;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import org.apache.age.jdbc.base.Agtype;
import org.apache.age.jdbc.base.AgtypeFactory;
import org.apache.age.jdbc.base.InvalidAgtypeException;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.postgresql.jdbc.PgConnection;

/**
 * Tests the different combinations that are possible when running Statements and Prepared
 * Statements with the AgType and shows how the JDBC needs to be setup to convert values to the
 * AgType.
 */
class AgtypeStatementTest {

    BaseDockerizedTest baseDockerizedTest = new BaseDockerizedTest();

    @BeforeEach
    public void setup() throws Exception {
        baseDockerizedTest.beforeAll();
    }

    @AfterEach
    void tearDown() throws Exception {
        baseDockerizedTest.afterAll();
    }

    /**
     * When a statement is run first, "ag_catalog"."agtype" needs to be added to the connection.
     *
     * @throws SQLException Throws an SQL Exception if the driver is unable to parse Agtype.
     */
    @Test
    void agTypeInStatementAsString() throws SQLException, InvalidAgtypeException {
        baseDockerizedTest.getConnection().addDataType("\"ag_catalog\".\"agtype\"", Agtype.class);
        //Step 1: Run a statement
        runStatementString(baseDockerizedTest.getConnection());
    }

    /**
     * When a Prepared statement is run first and the agtype is a parameter, agtype needs to be
     * added to the connection.
     *
     * @throws SQLException Throws an SQL Exception if the driver is unable to parse Agtype.
     */
    @Test
    void asTypeInPreparedStatementAsParameter() throws SQLException, InvalidAgtypeException {
        baseDockerizedTest.getConnection().addDataType("agtype", Agtype.class);
        //Step 1: Run a Prepared Statement
        runPreparedStatementParameter(baseDockerizedTest.getConnection());
    }

    /**
     * When a Prepared statement is run first and the agtype is not a parameter, but in the string,
     * "ag_catalog"."agtype" needs to be added to the connection.
     *
     * @throws SQLException Throws an SQL Exception if the driver is unable to parse Agtype.
     */
    @Test
    void asTypeInPreparedStatementAsString() throws SQLException, InvalidAgtypeException {
        baseDockerizedTest.getConnection().addDataType("\"ag_catalog\".\"agtype\"", Agtype.class);

        runPreparedStatementString(baseDockerizedTest.getConnection());
    }

    /**
     * When a Prepared statement is run and agType is both a string and a parameter, agtype needs to
     * be added to the connection, but "ag_catalog."agtype" does not need to be added.
     *
     * @throws SQLException Throws an SQL Exception if the driver is unable to parse Agtype.
     */
    @Test
    void agTypeInPreparedStatementAsStringAndParam() throws SQLException, InvalidAgtypeException {

        baseDockerizedTest.getConnection().addDataType("agtype", Agtype.class);

        //Step 1 Run a Prepared Statement when AgType is a String and a Parameter.
        runPreparedStatementParameterAndString(baseDockerizedTest.getConnection());

    }

    /**
     * When a statement is run first, "ag_catalog"."agType" needs to be added to the connection, no
     * need to add agtype for running a Prepared Statement afterward.
     *
     * @throws SQLException Throws an SQL Exception if the driver is unable to parse Agtype.
     */
    @Test
    void asTypeInStatementThenPreparedStatement() throws SQLException, InvalidAgtypeException {
        baseDockerizedTest.getConnection().addDataType("\"ag_catalog\".\"agtype\"", Agtype.class);
        //Step 1: Run a statement
        runStatementString(baseDockerizedTest.getConnection());
        //Step 2: Run a Prepared Statement, where AgType is a parameter
        runPreparedStatementParameter(baseDockerizedTest.getConnection());
    }

    /**
     * When a Prepared statement is run first, agtype needs to be added to the connection, no need
     * to add "ag_catalog"."agType" for running a Statement afterward.
     *
     * @throws SQLException Throws an SQL Exception if the driver is unable to parse Agtype.
     */
    @Test
    void asTypeInPreparedStatementThenStatement() throws SQLException, InvalidAgtypeException {
        //Add the agtype Data Type.
        baseDockerizedTest.getConnection().addDataType("agtype", Agtype.class);
        //Step 1: Run a Prepared Statement
        runPreparedStatementParameter(baseDockerizedTest.getConnection());
        //Step 2: Run a Statement
        runStatementString(baseDockerizedTest.getConnection());
    }

    /*
     *     Helper Methods
     */
    private void runStatementString(PgConnection conn) throws SQLException, InvalidAgtypeException {
        ResultSet rs;
        Statement stmt = conn.createStatement();
        stmt.execute("SELECT '1'::ag_catalog.agtype");
        rs = stmt.getResultSet();
        assertTrue(rs.next());
        Agtype returnedAgtype = (Agtype) rs.getObject(1);
        assertEquals(1, returnedAgtype.getInt());
    }

    private void runPreparedStatementParameter(PgConnection conn) throws SQLException,
        InvalidAgtypeException {
        PreparedStatement ps = conn.prepareStatement("SELECT ?");

        ps.setObject(1, AgtypeFactory.create(1));

        ps.executeQuery();
        ResultSet rs = ps.getResultSet();

        assertTrue(rs.next());

        Agtype returnedAgtype = (Agtype) rs.getObject(1);
        assertEquals(1, returnedAgtype.getInt());
    }

    private void runPreparedStatementParameterAndString(PgConnection conn) throws SQLException,
        InvalidAgtypeException {
        PreparedStatement ps = conn
            .prepareStatement("SELECT ?, '1'::ag_catalog.agtype");
        Agtype agType = new Agtype();

        agType.setValue("1");
        ps.setObject(1, agType);

        ps.executeQuery();
        ResultSet rs = ps.getResultSet();
        assertTrue(rs.next());

        Agtype returnedAgtype = (Agtype) rs.getObject(1);
        assertEquals(1, returnedAgtype.getInt());

        returnedAgtype = (Agtype) rs.getObject(2);
        assertEquals(1, returnedAgtype.getInt());
    }

    private void runPreparedStatementString(PgConnection conn) throws SQLException,
        InvalidAgtypeException {
        PreparedStatement ps = conn
            .prepareStatement("SELECT ?, '1'::ag_catalog.agtype");

        ps.setInt(1, 1);
        ps.executeQuery();
        ResultSet rs = ps.getResultSet();
        assertTrue(rs.next());
        Agtype returnedAgtype = (Agtype) rs.getObject(2);

        assertEquals(1, returnedAgtype.getInt());
    }
}
