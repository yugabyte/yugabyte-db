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

import java.sql.DriverManager;
import java.sql.Statement;
import org.apache.age.jdbc.base.Agtype;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.TestInstance;
import org.junit.jupiter.api.TestInstance.Lifecycle;
import org.postgresql.jdbc.PgConnection;
import org.testcontainers.containers.GenericContainer;
import org.testcontainers.utility.DockerImageName;

@TestInstance(Lifecycle.PER_CLASS)
public class BaseDockerizedTest {

    private PgConnection connection;
    private GenericContainer<?> agensGraphContainer;


    public PgConnection getConnection() {
        return connection;
    }

    @AfterAll
    public void afterAll() throws Exception {
        connection.close();
        agensGraphContainer.stop();
    }

    @BeforeAll
    public void beforeAll() throws Exception {
        String CORRECT_DB_PASSWORDS = "postgres";

        agensGraphContainer = new GenericContainer<>(DockerImageName
            .parse("apache/age:dev_snapshot_PG15"))
            .withEnv("POSTGRES_PASSWORD", CORRECT_DB_PASSWORDS)
            .withExposedPorts(5432);
        agensGraphContainer.start();

        int mappedPort = agensGraphContainer.getMappedPort(5432);
        String jdbcUrl = String
            .format("jdbc:postgresql://%s:%d/%s", "localhost", mappedPort, "postgres");

        try {
            this.connection = DriverManager.getConnection(jdbcUrl, "postgres", CORRECT_DB_PASSWORDS)
                         .unwrap(PgConnection.class);
            this.connection.addDataType("agtype", Agtype.class);
        } catch (Exception e) {
            System.out.println(e);
        }
        try (Statement statement = connection.createStatement()) {
            statement.execute("CREATE EXTENSION IF NOT EXISTS age;");
            statement.execute("LOAD 'age'");
            statement.execute("SET search_path = ag_catalog, \"$user\", public;");
            statement.execute("SELECT create_graph('cypher');");
        }
    }
}
