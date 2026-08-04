// Copyright (c) YugaByte, Inc.

package db.migration.default_.postgres

import org.flywaydb.core.api.migration.{BaseJavaMigration, Context}

class V197__API_Token_Encryption_Update extends BaseJavaMigration {
  override def migrate(context: Context): Unit = {
    val connection = context.getConnection

    // Migrate the users table to use new encryption
    connection.createStatement.executeUpdate("ALTER TABLE users ALTER COLUMN auth_token TYPE" +
      " bytea USING pgp_sym_encrypt(auth_token::text, 'users::auth_token')")
  }
}
