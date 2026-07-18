// Copyright (c) YugaByte, Inc.

package db.migration.default_.common

import org.flywaydb.core.api.migration.{BaseJavaMigration, Context}

import java.util.UUID
import play.api.libs.json._

import scala.util.control.Breaks._

class V62__Insert_Alert_Definition_Clock_Skew extends BaseJavaMigration {

  override def migrate(context: Context): Unit = {
    val connection = context.getConnection
    val selectStmt = "SELECT u.universe_uuid, customer_id, universe_details_json FROM universe u " +
      "LEFT JOIN alert_definition a ON u.universe_uuid=a.universe_uuid AND " +
      "a.name='Clock Skew Alert' WHERE a.universe_uuid IS NULL"
    val resultSet = connection.createStatement().executeQuery(selectStmt)

    while (resultSet.next()) {
      breakable {
        val univUuid = resultSet.getString("universe_uuid")
        val customerId = resultSet.getString("customer_id")
        val univDetails = Json.parse(resultSet.getString("universe_details_json"))
        val nodePrefix = univDetails \ "nodePrefix"
        if (!nodePrefix.isInstanceOf[JsDefined]) break()

        val customerSet = connection.createStatement().executeQuery(s"SELECT uuid from customer " +
          s"WHERE id = '$customerId' LIMIT 1")
        if (!customerSet.next()) break()

        val customerUuid = customerSet.getString("uuid")
        val definitionUuid = UUID.randomUUID()
        val query = s"max by (node_prefix) (max_over_time(hybrid_clock_skew" +
          s"{node_prefix=${'"'}${nodePrefix.get.as[String]}${'"'}}[10m])) / 1000 > " +
          s"{{ yb.alert.max_clock_skew_ms }}"

        connection.createStatement().execute(s"INSERT INTO alert_definition (uuid, query, name, " +
          s"universe_uuid, is_active, customer_uuid) VALUES ('$definitionUuid', " +
          s"'$query', 'Clock Skew Alert', '$univUuid', true, '$customerUuid')")
      }
    }
  }
}
