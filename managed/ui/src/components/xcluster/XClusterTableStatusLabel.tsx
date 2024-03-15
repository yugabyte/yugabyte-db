import { useQuery } from 'react-query';
import clsx from 'clsx';

import { AlertName, XClusterTableStatus } from './constants';
import { assertUnreachableCase } from '../../utils/errorHandlingUtils';
import { fetchReplicationLag, queryLagMetricsForTable } from '../../actions/xClusterReplication';
import { getAlertConfigurations } from '../../actions/universe';
import { getLatestMaxNodeLag } from './ReplicationUtils';
import { YBLoadingCircleIcon } from '../common/indicators';

import styles from './XClusterTableStatusLabel.module.scss';
import { alertConfigQueryKey, metricQueryKey } from '../../redesign/helpers/api';

interface XClusterTableStatusProps {
  status: XClusterTableStatus;
  streamId: string;
  sourceUniverseTableUuid: string;
  sourceUniverseNodePrefix: string;
  sourceUniverseUuid: string;
}

const OPERATIONAL_LABEL = (
  <span className={clsx(styles.label, styles.ready)}>
    Operational
    <i className="fa fa-check" />
  </span>
);
const WARNING_LABEL = (
  <span className={clsx(styles.label, styles.warning)}>
    Warning
    <i className="fa fa-exclamation-triangle" />
  </span>
);
const FAILED_LABEL = (
  <span className={clsx(styles.label, styles.error)}>
    Failed
    <i className="fa fa-exclamation-circle" />
  </span>
);
const ERROR_LABEL = (
  <span className={clsx(styles.label, styles.error)}>
    Error
    <i className="fa fa-exclamation-circle" />
  </span>
);
const IN_PROGRESS_LABEL = (
  <span className={clsx(styles.label, styles.inProgress)}>
    In Progress
    <i className="fa fa-spinner fa-spin" />
  </span>
);
const VALIDATED_LABEL = (
  <span className={clsx(styles.label, styles.inProgress)}>
    Validated
    <i className="fa fa-spinner fa-spin" />
  </span>
);
const BOOTSTRAPPING_LABEL = (
  <span className={clsx(styles.label, styles.inProgress)}>
    Bootstrapping
    <i className="fa fa-spinner fa-spin" />
  </span>
);
const UNABLE_TO_FETCH_LABEL = (
  <span className={clsx(styles.label, styles.warning)}>
    Unable To Fetch
    <i className="fa fa-exclamation-triangle" />
  </span>
);

export const XClusterTableStatusLabel = ({
  status,
  streamId,
  sourceUniverseTableUuid,
  sourceUniverseNodePrefix,
  sourceUniverseUuid
}: XClusterTableStatusProps) => {
  const alertConfigFilter = {
    name: AlertName.REPLICATION_LAG,
    targetUuid: sourceUniverseUuid
  };
  const maxAcceptableLagQuery = useQuery(alertConfigQueryKey.list(alertConfigFilter), () =>
    getAlertConfigurations(alertConfigFilter)
  );

  const replciationLagMetricRequestParams = {
    nodePrefix: sourceUniverseNodePrefix,
    streamId,
    tableId: sourceUniverseTableUuid
  };
  const tableReplicationLagQuery = useQuery(
    metricQueryKey.detail(replciationLagMetricRequestParams),
    () => fetchReplicationLag(replciationLagMetricRequestParams)
  );

  switch (status) {
    case XClusterTableStatus.RUNNING: {
      if (
        tableReplicationLagQuery.isLoading ||
        tableReplicationLagQuery.isIdle ||
        maxAcceptableLagQuery.isLoading ||
        maxAcceptableLagQuery.isIdle
      ) {
        return <YBLoadingCircleIcon />;
      }
      if (tableReplicationLagQuery.isError || maxAcceptableLagQuery.isError) {
        return <span>-</span>;
      }

      const maxAcceptableLag = Math.min(
        ...maxAcceptableLagQuery.data.map(
          (alertConfig: any): number => alertConfig.thresholds.SEVERE.threshold
        )
      );
      const maxNodeLag = getLatestMaxNodeLag(tableReplicationLagQuery.data);
      return maxNodeLag === undefined || maxNodeLag > maxAcceptableLag
        ? WARNING_LABEL
        : OPERATIONAL_LABEL;
    }
    case XClusterTableStatus.WARNING:
      return WARNING_LABEL;
    case XClusterTableStatus.FAILED:
      return FAILED_LABEL;
    case XClusterTableStatus.ERROR:
      return ERROR_LABEL;
    case XClusterTableStatus.UPDATING:
      return IN_PROGRESS_LABEL;
    case XClusterTableStatus.VALIDATED:
      return VALIDATED_LABEL;
    case XClusterTableStatus.BOOTSTRAPPING:
      return BOOTSTRAPPING_LABEL;
    case XClusterTableStatus.UNABLE_TO_FETCH:
      return UNABLE_TO_FETCH_LABEL;
    default:
      return assertUnreachableCase(status);
  }
};
