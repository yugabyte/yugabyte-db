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

package org.kududb.spark

import org.apache.spark.rdd.RDD
import org.apache.spark.sql.sources._
import org.apache.spark.sql.types._
import org.apache.spark.sql.{Row, SQLContext}
import org.kududb.Type
import org.kududb.annotations.InterfaceStability
import org.kududb.client.RowResult

import scala.collection.JavaConverters._
import scala.collection.immutable.HashMap

/**
  * DefaultSource for integration with Spark's dataframe datasources.
  * This class will produce a relationProvider based on input given to it from spark.
  */
@InterfaceStability.Unstable
class DefaultSource extends RelationProvider {

  val TABLE_KEY = "kudu.table"
  val KUDU_MASTER = "kudu.master"

  /**
    * Construct a BaseRelation using the provided context and parameters.
    *
    * @param sqlContext SparkSQL context
    * @param parameters parameters given to us from SparkSQL
    * @return           a BaseRelation Object
    */
  override def createRelation(sqlContext: SQLContext,
                              parameters: Map[String, String]):
  BaseRelation = {
    val tableName = parameters.get(TABLE_KEY)
    if (tableName.isEmpty) {
      throw new IllegalArgumentException(s"Invalid value for $TABLE_KEY '$tableName'")
    }

    val kuduMaster = parameters.getOrElse(KUDU_MASTER, "localhost")

    new KuduRelation(tableName.get, kuduMaster)(sqlContext)
  }
}

/**
  * Implementation of Spark BaseRelation.
  *
  * @param tableName Kudu table that we plan to read from
  * @param kuduMaster Kudu master addresses
  * @param sqlContext SparkSQL context
  */
@InterfaceStability.Unstable
class KuduRelation(val tableName: String,
                   val kuduMaster: String)(
                   @transient val sqlContext: SQLContext)
  extends BaseRelation with PrunedFilteredScan with Serializable {

  val typesMapping = HashMap[Type, DataType](
    Type.INT16 -> IntegerType,
    Type.INT32 -> IntegerType,
    Type.INT64 -> LongType,
    Type.FLOAT -> FloatType,
    Type.DOUBLE -> DoubleType,
    Type.STRING -> StringType,
    Type.TIMESTAMP -> TimestampType,
    Type.BINARY -> BinaryType
  )

  // Using lazy val for the following because we can't serialize them but we need them once we
  // deserialize them.
  @transient lazy val kuduContext = new KuduContext(kuduMaster)
  @transient lazy val kuduTable = kuduContext.syncClient.openTable(tableName)
  @transient lazy val tableColumns = kuduTable.getSchema.getColumns.asScala
  @transient lazy val kuduSchemaColumnMap = tableColumns.map(c => (c.getName, c)).toMap

  /**
    * Generates a SparkSQL schema object so SparkSQL knows what is being
    * provided by this BaseRelation.
    *
    * @return schema generated from the Kudu table's schema
    */
  override def schema: StructType = {
    val metadataBuilder = new MetadataBuilder()

    val structFieldArray: Array[StructField] =
      tableColumns.map { columnSchema =>
        val columnSparkSqlType = typesMapping.getOrElse(
          columnSchema.getType,
          throw new IllegalArgumentException(s"Unsupported column type: ${columnSchema.getType}"))

        val metadata = metadataBuilder.putString("name", columnSchema.getName).build()
        new StructField(columnSchema.getName, columnSparkSqlType,
                        nullable = columnSchema.isNullable, metadata)
      }.toArray

    new StructType(structFieldArray)
  }

  /**
    * Build the RDD to scan rows.
    *
    * @param requiredColumns clumns that are being requested by the requesting query
    * @param filters         filters that are being applied by the requesting query
    * @return                RDD will all the results from Kudu
    */
  override def buildScan(requiredColumns: Array[String], filters: Array[Filter]): RDD[Row] = {
    kuduContext.kuduRDD(sqlContext.sparkContext, tableName, requiredColumns).map { row =>
      // TODO use indexes instead of column names since it requires one less mapping.
      Row.fromSeq(requiredColumns.map(column => getKuduValue(row, column)))
    }
  }

  private def getKuduValue(row: RowResult, columnName: String): Any = {
    val columnType = kuduSchemaColumnMap.getOrElse(columnName,
      throw new IllegalArgumentException(s"Couldn't find column '$columnName'")).getType

    columnType match {
      case Type.BINARY => row.getBinary(columnName)
      case Type.BOOL => row.getBoolean(columnName)
      case Type.DOUBLE => row.getDouble(columnName)
      case Type.FLOAT => row.getFloat(columnName)
      case Type.INT16 => row.getShort(columnName)
      case Type.INT32 => row.getInt(columnName)
      case Type.INT64 => row.getLong(columnName)
      case Type.INT8 => row.getByte(columnName)
      case Type.TIMESTAMP => row.getLong(columnName)
      case Type.STRING => row.getString(columnName)
      case _ => throw new IllegalArgumentException(s"Type not supported: '${columnType.getName}'")
    }
  }
}