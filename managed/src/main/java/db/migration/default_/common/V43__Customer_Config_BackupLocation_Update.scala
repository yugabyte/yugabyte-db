// Copyright (c) YugaByte, Inc.

package db.migration.default_.common

import org.flywaydb.core.api.migration.{BaseJavaMigration, Context}

import play.api.libs.json._

class V43__Customer_Config_BackupLocation_Update extends BaseJavaMigration {
  override def migrate(context: Context): Unit = {
    val connection = context.getConnection
    val selectStmt = "SELECT config_uuid, data FROM customer_config WHERE type = 'STORAGE'"
    val resultSet = connection.createStatement().executeQuery(selectStmt)

    while (resultSet.next()) {
      val configUuid = resultSet.getString("config_uuid")
      var data = Json.parse(resultSet.getString("data"))
      var backup_location : JsValue = JsNull
      val keyList = List("NFS_PATH", "S3_BUCKET")
      for (key <- keyList) {
        if ((data \ key).isInstanceOf[JsDefined]) {
            backup_location = (data \ key).get
            data = data.as[JsObject] - key
        }
      }
      data = data.as[JsObject] + ("BACKUP_LOCATION" -> backup_location)
      connection.createStatement().execute(s"UPDATE customer_config SET data = " +
        s"'${data.toString()}' WHERE config_uuid = '$configUuid'")
    }
  }
}
