import { useQuery } from 'react-query';
import clsx from 'clsx';

import { REPLICATION_LAG_ALERT_NAME, XClusterTableStatus } from './constants';
import { assertUnreachableCase } from '../../utils/errorHandlingUtils';
import { queryLagMetricsForTable } from '../../actions/xClusterReplication';
import { getAlertConfigurations } from '../../actions/universe';
import { getLatestMaxNodeLag } from './ReplicationUtils';
import { YBLoadingCircleIcon } from '../common/indicators';

import styles from './XClusterTableStatusLabel.module.scss';

interface XClusterTableStatusProps {
  status: XClusterTableStatus;
  streamId: string;
  tableUUID: string;
  nodePrefix: string;
  universeUUID: string;
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
  tableUUID,
  nodePrefix,
  universeUUID
}: XClusterTableStatusProps) => {
  const alertConfigFilter = {
    name: REPLICATION_LAG_ALERT_NAME,
    targetUuid: universeUUID
  };
  const maxAcceptableLagQuery = useQuery(['alert', 'configurations', alertConfigFilter], () =>
    getAlertConfigurations(alertConfigFilter)
  );
  const tableLagQuery = useQuery(
    ['xcluster-metric', nodePrefix, tableUUID, streamId, 'metric'],
    () => queryLagMetricsForTable(streamId, tableUUID, nodePrefix)
  );

  switch (status) {
    case XClusterTableStatus.RUNNING: {
      if (
        tableLagQuery.isLoading ||
        tableLagQuery.isIdle ||
        maxAcceptableLagQuery.isLoading ||
        maxAcceptableLagQuery.isIdle
      ) {
        return <YBLoadingCircleIcon />;
      }
      if (tableLagQuery.isError || maxAcceptableLagQuery.isError) {
        return <span>-</span>;
      }

      const maxAcceptableLag = Math.min(
        ...maxAcceptableLagQuery.data.map(
          (alertConfig: any): number => alertConfig.thresholds.SEVERE.threshold
        )
      );
      const maxNodeLag = getLatestMaxNodeLag(tableLagQuery.data);
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
