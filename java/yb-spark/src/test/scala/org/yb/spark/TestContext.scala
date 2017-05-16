/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package org.yb.spark

import com.google.common.collect.ImmutableList
import org.apache.spark.SparkContext
import org.yb.ColumnSchema.ColumnSchemaBuilder
import org.yb.client.YBClient.YBClientBuilder
import org.yb.client.MiniYBCluster
import org.yb.client.MiniYBClusterBuilder
import org.yb.client.{CreateTableOptions, YBClient, YBTable}
import org.yb.{Schema, Type}
import org.scalatest.{BeforeAndAfterAll, Suite}

trait TestContext extends BeforeAndAfterAll { self: Suite =>

  var sc: SparkContext = null
  var miniCluster: MiniYBCluster = null
  var ybClient: YBClient = null
  var table: YBTable = null
  var ybContext: YBContext = null

  val tableName = "test"

  lazy val schema: Schema = {
    val columns = ImmutableList.of(
      new ColumnSchemaBuilder("key", Type.INT32).key(true).build(),
      new ColumnSchemaBuilder("c1_i", Type.INT32).build(),
      new ColumnSchemaBuilder("c2_s", Type.STRING).build())
    new Schema(columns)
  }

  override def beforeAll() {
    miniCluster = new MiniYBClusterBuilder()
      .numMasters(1)
      .numTservers(1)
      .build()
    val envMap = Map[String,String](("Xmx", "512m"))

    sc = new SparkContext("local[2]", "test", null, Nil, envMap)

    ybClient = new YBClientBuilder(miniCluster.getMasterAddresses).build()
    assert(miniCluster.waitForTabletServers(1))

    ybContext = new YBContext(miniCluster.getMasterAddresses)

    val tableOptions = new CreateTableOptions().setNumReplicas(1)
    table = ybClient.createTable(tableName, schema, tableOptions)
  }

  override def afterAll() {
    if (ybClient != null) ybClient.shutdown()
    if (miniCluster != null) miniCluster.shutdown()
    if (sc != null) sc.stop()
  }

  def insertRows(rowCount: Integer) {
    val ybSession = ybClient.newSession()

    for (i <- 1 to rowCount) {
      val insert = table.newInsert
      val row = insert.getRow
      row.addInt(0, i)
      row.addInt(1, i)
      row.addString(2, i.toString)
      ybSession.apply(insert)
    }
  }
}
