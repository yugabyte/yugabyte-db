// Copyright (c) YugaByte, Inc.

package db.migration.default_.common

import org.flywaydb.core.api.migration.{BaseJavaMigration, Context}

import play.api.libs.json._

class V46__AccessKey_SshPort_Update extends BaseJavaMigration {
  override def migrate(context: Context): Unit = {
    val connection = context.getConnection
    var selectStmt = "SELECT ak.key_code, ak.key_info FROM access_key ak JOIN provider p " +
        "ON p.uuid = ak.provider_uuid AND p.code = 'onprem'"
    var resultSet = connection.createStatement().executeQuery(selectStmt)

    while (resultSet.next()) {
      val keyCode = resultSet.getString("key_code")
      var keyInfo = Json.parse(resultSet.getString("key_info"))

      val sshPort = new JsNumber(22)
      keyInfo = keyInfo.as[JsObject] + ("sshPort" -> sshPort)

      connection.createStatement().execute("UPDATE access_key SET key_info = " +
        s"'${keyInfo.toString()}' WHERE key_code = '$keyCode'")
    }
  }
}
