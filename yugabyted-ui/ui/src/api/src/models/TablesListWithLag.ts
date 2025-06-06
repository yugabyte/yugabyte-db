// tslint:disable
/**
 * Yugabyte Cloud
 * YugabyteDB as a Service
 *
 * The version of the OpenAPI document: v1
 * Contact: support@yugabyte.com
 *
 * NOTE: This class is auto generated by OpenAPI Generator (https://openapi-generator.tech).
 * https://openapi-generator.tech
 * Do not edit the class manually.
 */




/**
 * Schema for table details in the namespace involved in xCluster replication.
 * @export
 * @interface TablesListWithLag
 */
export interface TablesListWithLag  {
  /**
   * Namespace to which the current table belongs.
   * @type {string}
   * @memberof TablesListWithLag
   */
  namespace?: string;
  /**
   * Unique identifier for the table.
   * @type {string}
   * @memberof TablesListWithLag
   */
  table_uuid: string;
  /**
   * Name of the table.
   * @type {string}
   * @memberof TablesListWithLag
   */
  table_name: string;
  /**
   * Indicates whether the table is currently checkpointing.
   * @type {boolean}
   * @memberof TablesListWithLag
   */
  is_checkpointing: boolean;
  /**
   * Indicates whether the table is part of the initial bootstrap process.
   * @type {boolean}
   * @memberof TablesListWithLag
   */
  is_part_of_initial_bootstrap: boolean;
  /**
   * The lag time in microseconds for sent replication data.
   * @type {number}
   * @memberof TablesListWithLag
   */
  async_replication_sent_lag_micros: number;
  /**
   * The lag time in microseconds for committed replication data.
   * @type {number}
   * @memberof TablesListWithLag
   */
  async_replication_committed_lag_micros: number;
}



