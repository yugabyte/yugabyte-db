// Copyright (c) YugaByte, Inc.

package db.migration.default_.common

import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType
import com.yugabyte.yw.commissioner.Common
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams
import org.flywaydb.core.api.migration.{BaseJavaMigration, Context}

import play.libs.Json

import scala.compat.java8.FunctionConverters.asJavaPredicate

class V208__Universe_Details_Fill_Storage_Type extends BaseJavaMigration {

  override def migrate(context: Context): Unit = {
    val connection = context.getConnection
    val selectStmt = "SELECT universe_uuid, universe_details_json FROM universe"
    val resultSet = connection.createStatement().executeQuery(selectStmt)

    while (resultSet.next()) {
      val univUuid = resultSet.getString("universe_uuid")
      val univDetails = Json.parse(resultSet.getString("universe_details_json"))
      val universeDefinition = Json.fromJson(univDetails, classOf[UniverseDefinitionTaskParams])
      val updated = universeDefinition.clusters.stream()
        .anyMatch(asJavaPredicate[UniverseDefinitionTaskParams.Cluster]
          (cluster => processCluster(cluster)))
      if (updated) {
        val newUnivDetails = Json.stringify(Json.toJson(universeDefinition))
        val statement = connection.prepareStatement(
          "UPDATE universe SET universe_details_json = ? WHERE universe_uuid = ?::uuid")
        statement.setString(1, newUnivDetails)
        statement.setString(2, univUuid)
        statement.execute()
      }
    }
  }

  def processCluster(cluster: UniverseDefinitionTaskParams.Cluster) : Boolean = {
    if (cluster.userIntent == null || cluster.userIntent.deviceInfo == null
      || cluster.userIntent.deviceInfo.storageType != null) {
      return false
    }
    if (cluster.placementInfo == null || cluster.placementInfo.cloudList == null) {
      return false
    }
    val newStorageType =
      if (cluster.placementInfo.cloudList.get(0).code.equals(Common.CloudType.aws.toString))
        StorageType.GP2 else StorageType.Scratch
    cluster.userIntent.deviceInfo.storageType = newStorageType
    true
  }
}
