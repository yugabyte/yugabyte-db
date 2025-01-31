import moment from 'moment';

import {
  BootstrapCategory,
  CUSTOM_METRIC_TIME_RANGE_OPTION,
  DROPDOWN_DIVIDER,
  MetricName,
  METRIC_TIME_RANGE_OPTIONS,
  XClusterTableEligibility,
  XClusterTableStatus,
  XCLUSTER_SUPPORTED_TABLE_TYPES
} from './constants';

import { MetricTrace, YBTable } from '../../redesign/helpers/dtos';
import { XClusterConfigNeedBootstrapPerTableResponse, XClusterTableDetails } from './dtos';

/**
 * XCluster supported table type.
 */
export type XClusterTableType = typeof XCLUSTER_SUPPORTED_TABLE_TYPES[number];

/**
 * Extra metadata to be merged into xCluster table details for use in YBA UI.
 */
type XClusterTableUiExtraMetadata = {
  // Stores the user facing string in the object for sorting/searching usage.
  statusLabel: string;
  replicationLag: number;
};

export type XClusterTable = YBTable &
  Omit<XClusterTableDetails, 'tableId'> &
  XClusterTableUiExtraMetadata;
/**
 * A table which is in the replication config but dropped from the database.
 */
export type XClusterTableWithoutDetails = Omit<XClusterTableDetails, 'tableId'> &
  XClusterTableUiExtraMetadata & {
    tableUUID: string;
    status: typeof XClusterTableStatus.DROPPED | typeof XClusterTableStatus.TABLE_INFO_MISSING;
  };
export type XClusterReplicationTable = XClusterTable | XClusterTableWithoutDetails;

//------------------------------------------------------------------------------------
// Table Selection Types

/**
 * This type stores details of a table's eligibility for xCluster replication.
 */
export type EligibilityDetails =
  | {
      status: typeof XClusterTableEligibility.ELIGIBLE_UNUSED;
    }
  | {
      status: typeof XClusterTableEligibility.ELIGIBLE_IN_CURRENT_CONFIG;
      xClusterConfigName: string;
    }
  | { status: typeof XClusterTableEligibility.INELIGIBLE_IN_USE; xClusterConfigName: string };

/**
 * YBTable with additional metadata for table selection.
 */
export interface IndexTableReplicationCandidate extends YBTable {
  eligibilityDetails: EligibilityDetails;
  isUnreplicatedTableInReplicatedNamespace: boolean;
  isDroppedOnTarget: boolean;
}
/**
 * YBTable with with additional metadata for table selection and an array of index tables.
 */
export interface MainTableReplicationCandidate extends YBTable {
  eligibilityDetails: EligibilityDetails;
  isUnreplicatedTableInReplicatedNamespace: boolean;
  isDroppedOnTarget: boolean;

  indexTables?: IndexTableReplicationCandidate[];
}
export type TableReplicationCandidate =
  | MainTableReplicationCandidate
  | IndexTableReplicationCandidate;

export type IndexTableRestartReplicationCandidate = XClusterTable;
export interface MainTableRestartReplicationCandidate extends XClusterTable {
  indexTables?: IndexTableRestartReplicationCandidate[];
}
export type TableRestartReplicationCandidate =
  | MainTableRestartReplicationCandidate
  | IndexTableRestartReplicationCandidate;

/**
 * Holds list of tables for a namespace and provides extra metadata.
 */
export interface NamespaceItem {
  uuid: string;
  name: string;
  tableEligibilityCount: {
    ineligible: number;
    eligibleInCurrentConfig: number;
  };
  sizeBytes: number;

  // Filtered table list currently shown to the user
  tables: MainTableReplicationCandidate[];
  // All tables under the namespace
  allTables: MainTableReplicationCandidate[];
}

/**
 * Structure for organizing tables by namespaces.
 */
export type ReplicationItems = {
  namespaces: Record<string, NamespaceItem>;

  // We store a set of table uuids at the top level to make it easy to check
  // if the list of table options matching the current search tokens contains a specific table uuid.
  searchMatchingTableUuids: Set<string>;
  searchMatchingNamespaceUuids: Set<string>;
};

export interface CategoryNeedBootstrapResponse {
  bootstrapCategory: BootstrapCategory;
  tableCount: number;
  tables: XClusterConfigNeedBootstrapPerTableResponse;
}

export interface CategorizedNeedBootstrapPerTableResponse {
  bootstrapTableUuids: string[];
  noBootstrapRequired: CategoryNeedBootstrapResponse;
  tableHasDataBidirectional: CategoryNeedBootstrapResponse;
  targetTableMissingBidirectional: CategoryNeedBootstrapResponse;
  tableHasData: CategoryNeedBootstrapResponse;
  targetTableMissing: CategoryNeedBootstrapResponse;
}

//------------------------------------------------------------------------------------

export type Metrics<MetricNameType extends MetricName> = {
  [metricName in MetricNameType]: {
    data: MetricTrace[];
    directURLs: string[];
    layout: {
      title: string;
      xaxis: {
        alias: { [x: string]: string };
        type: string;
      };
      yaxis: {
        alias: { [x: string]: string };
        ticksuffix: string;
      };
    };
    queryKey: string;
  };
};

//------------------------------------------------------------------------------------

// Time range selector types.

export type MetricTimeRangeOption = Exclude<
  typeof METRIC_TIME_RANGE_OPTIONS[number],
  typeof DROPDOWN_DIVIDER
>;

export type StandardMetricTimeRangeOption = Exclude<
  MetricTimeRangeOption,
  typeof CUSTOM_METRIC_TIME_RANGE_OPTION
>;

export interface MetricTimeRange {
  startMoment: moment.Moment;
  endMoment: moment.Moment;
}
