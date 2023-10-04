// Copyright (c) YugaByte, Inc.


package db.migration.default.common

import com.yugabyte.yw.common.Util;
import java.sql.{Connection, PreparedStatement}
import java.util.UUID
import org.flywaydb.core.api.migration.jdbc.JdbcMigration
import play.api.libs.json._
import play.api.Logger
import scala.collection.mutable


class V299__Set_Universe_Node_UUID extends JdbcMigration {

  /**
   * Utility method to recursively apply a modification function to a json value and all its
   * elements. Modification function is applied top-down (i.e. applied to parents before children).
   *
   * @param json     the json value
   * @param path     the location of the current value inside the root json object
   * @param modifier the function to be applied to every json value, takes the current path as the
   *                 second argument to decide if/how to modify the current value.
   * @return the modified json value
   */
  def processJson(json: JsValue, path: Array[Any] = Array())
                 (implicit modifier: (JsValue, Array[Any]) => JsValue): JsValue = {
    modifier(json, path) match {
      case JsObject(underlying) =>
        JsObject(underlying.map(kv => kv._1 -> processJson(kv._2, path :+ kv._1)))
      case JsArray(value) =>
        JsArray(value.zipWithIndex.map(vi => processJson(vi._1, path :+ vi._2)))
      case jsval => jsval
    }
  }

  def updateNodeUuid(universeUuid: UUID,
                     nodeInstanceNameToNodeUuidMap: mutable.Map[String, UUID],
                     clusterUuidToProviderTypeMap: mutable.Map[String, String])
                    (json: JsValue, path: Array[Any]): JsValue = {
    path match {
      case Array("nodeDetailsSet", _) if json.as[JsObject].keys.contains("placementUuid")
        && json.as[JsObject].keys.contains("nodeName")
        && !json.as[JsObject].keys.contains("nodeUuid") =>
        val placementUuid = (json \ "placementUuid").get.as[String]
        val nodeName = (json \ "nodeName").get.as[String]
        var newJson = json
        val  providerTypeOpt = clusterUuidToProviderTypeMap.get(placementUuid)
        if (providerTypeOpt.isDefined) {
          var nodeUuid: UUID = null
          val providerType = providerTypeOpt.get
          providerType match {
            case "onprem" =>
              val nodeUuidOpt = nodeInstanceNameToNodeUuidMap.get(nodeName)
              if (nodeUuidOpt.isDefined) {
                nodeUuid = nodeUuidOpt.get
              } else {
                Logger.warn(s"Node UUID is not found for on-prem node $nodeName")
              }
            case _ => nodeUuid = Util.generateNodeUUID(universeUuid, nodeName)
          }
          if (nodeUuid != null) {
            newJson = json.as[JsObject] + ("nodeUuid" -> Json.toJson(nodeUuid))
          }
        } else {
          Logger.warn(s"Node UUID cannot be assigned to node $nodeName")
        }
        newJson
      case _ => json
    }
  }
  private def getNodeInstanceNameToUuidMap(connection: Connection): mutable.Map[String, UUID]= {
    val nodeInstanceNameToNodeUuidMap: mutable.Map[String, UUID] = mutable.Map()
    val nodeInstanceRs = connection.createStatement.executeQuery("SELECT node_uuid, node_name from node_instance where in_use = true")
    while (nodeInstanceRs.next()) {
      val nodeUuid = nodeInstanceRs.getString("node_uuid")
      val nodeName = nodeInstanceRs.getString("node_name")
      if (nodeName == null || nodeName.isEmpty) {
        Logger.warn(s"Node name is missing for $nodeUuid but it is in-use")
      } else {
        nodeInstanceNameToNodeUuidMap(nodeName) = UUID.fromString(nodeUuid)
      }
    }
    nodeInstanceNameToNodeUuidMap
  }

  private def getClusterUuidToProviderTypeMap(universeDetails: JsValue): mutable.Map[String, String]= {
    val clusterUuidToProviderTypeMap: mutable.Map[String, String] = mutable.Map()
    val clusters = (universeDetails \ "clusters").get.as[List[JsValue]]
    for (cluster <- clusters) {
      val clusterUuid = (cluster \ "uuid").get.as[String]
      val userIntentLookup = cluster \ "userIntent"
      if (!userIntentLookup.isDefined || userIntentLookup.isEmpty) {
        Logger.warn(s"User intent is invalid for cluster $clusterUuid")
      } else {
        val providerTypeLookup = userIntentLookup.get \ "providerType"
        if (providerTypeLookup.isDefined) {
          clusterUuidToProviderTypeMap(clusterUuid) = providerTypeLookup.get.as[String]
        } else {
          Logger.warn(s"Provider type is missing for $clusterUuid")
        }
      }
    }
    clusterUuidToProviderTypeMap
  }

  override def migrate(connection: Connection): Unit = {
    val nodeInstanceNameToUuidMap = getNodeInstanceNameToUuidMap(connection)
    val updateUniverseDetailsStmt = connection.prepareStatement("UPDATE universe SET universe_details_json = ? WHERE universe_uuid = ?")
    val universeRs = connection.createStatement.executeQuery("SELECT universe_uuid, universe_details_json FROM universe")
    while (universeRs.next()) {
      val universeUuid = UUID.fromString(universeRs.getString("universe_uuid"))
      val universeDetails = Json.parse(universeRs.getString("universe_details_json"))
      val clusterUuidToProviderTypeMap = getClusterUuidToProviderTypeMap(universeDetails)
      val newUniverseDetails = processJson(universeDetails)(
        updateNodeUuid(universeUuid, nodeInstanceNameToUuidMap, clusterUuidToProviderTypeMap)
      )
      if (universeDetails ne newUniverseDetails) {
        // Reference comparison if it has changed.
        updateUniverseDetailsStmt.setString(1, newUniverseDetails.toString())
        updateUniverseDetailsStmt.setObject(2, universeUuid)
        updateUniverseDetailsStmt.executeUpdate
      }
    }
  }
}
