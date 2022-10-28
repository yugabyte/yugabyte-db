// Copyright (c) YugaByte, Inc.

package db.migration.default.postgres

import java.sql.Connection

import org.flywaydb.core.api.migration.jdbc.JdbcMigration

class V197__API_Token_Encryption_Update extends JdbcMigration {
  override def migrate(connection: Connection): Unit = {

    // Migrate the users table to use new encryption
    connection.createStatement.executeUpdate("ALTER TABLE users ALTER COLUMN auth_token TYPE" +
      " bytea USING pgp_sym_encrypt(auth_token::text, 'users::auth_token')")
  }
}
