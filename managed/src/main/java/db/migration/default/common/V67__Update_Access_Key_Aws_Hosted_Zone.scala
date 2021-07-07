// Copyright (c) YugaByte, Inc.

package db.migration.default.common

import java.sql.Connection
import java.util.UUID
import play.api.libs.json._
import org.flywaydb.core.api.migration.jdbc.JdbcMigration

class V67__Update_Access_Key_Aws_Hosted_Zone extends JdbcMigration {

  override def migrate(connection: Connection): Unit = {
    val selectStmt = "SELECT uuid, config FROM provider WHERE code = 'aws' AND CONFIG IS NOT NULL"
    val resultSet = connection.createStatement().executeQuery(selectStmt)

    while (resultSet.next()) {
      val uuid = resultSet.getString("uuid")
      var config = Json.parse(resultSet.getString("config"))
      var migrationNeeded : Boolean = false
      var key = "AWS_HOSTED_ZONE_ID"
      if ((config \ key).isInstanceOf[JsDefined]) {
        val value = (config \ key).get
        config = config.as[JsObject] - key
        config = config.as[JsObject] + ("HOSTED_ZONE_ID" -> value)
        migrationNeeded = true
      }
      key = "AWS_HOSTED_ZONE_NAME"
      if ((config \ key).isInstanceOf[JsDefined]) {
        val value = (config \ key).get
        config = config.as[JsObject] - key
        config = config.as[JsObject] + ("HOSTED_ZONE_NAME" -> value)
        migrationNeeded = true
      }
      if (migrationNeeded) {
        connection.createStatement().execute(s"UPDATE provider SET config = " +
          s"'${config.toString()}' WHERE uuid = '$uuid'")
      }
    }
  }
}
