import clsx from 'clsx';
import { makeStyles, Typography } from '@material-ui/core';
import { XClusterConfigStatus } from './constants';
import { assertUnreachableCase } from '../../utils/errorHandlingUtils';
import { getTableCountsOfConcern } from './ReplicationUtils';
import { useTranslation } from 'react-i18next';

import { XClusterConfig } from './dtos';

import styles from './XClusterConfigStatusLabel.module.scss';
import { usePillStyles } from '../../redesign/styles/styles';

interface XClusterConfigStatusProps {
  xClusterConfig: XClusterConfig;
}

const IN_PROGRESS_LABEL = (
  <span className={clsx(styles.label, styles.inProgress)}>
    <i className="fa fa-spinner fa-spin" />
    In Progress
  </span>
);
const PAUSED_LABEL = (
  <span className={clsx(styles.label, styles.paused)}>
    <i className="fa fa-pause-circle-o" />
    Paused
  </span>
);
const ENABLED_LABEL = (
  <span className={clsx(styles.label, styles.running)}>
    <i className="fa fa-check-circle" />
    Enabled
  </span>
);
const FAILED_LABEL = (
  <span className={clsx(styles.label, styles.failed)}>
    <i className="fa fa-exclamation-triangle" />
    Failed
  </span>
);
const DELETION_FAILED_LABEL = (
  <span className={clsx(styles.label, styles.deletionFailed)}>
    <i className="fa fa-close" />
    Deletion Failed
  </span>
);
const DRAINED_DATA_LABEL = (
  <span className={clsx(styles.label, styles.inProgress)}>
    <i className="fa fa-spinner fa-spin" />
    Drained Data
  </span>
);

const useStyles = makeStyles((theme) => ({
  pillContainer: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center'
  }
}));

export const XClusterConfigStatusLabel = ({ xClusterConfig }: XClusterConfigStatusProps) => {
  const pillClasses = usePillStyles();
  const classes = useStyles();
  const { t } = useTranslation('translation');

  let statusLabel = null;
  switch (xClusterConfig.status) {
    case XClusterConfigStatus.INITIALIZED:
    case XClusterConfigStatus.UPDATING:
      statusLabel = IN_PROGRESS_LABEL;
      break;
    case XClusterConfigStatus.RUNNING:
      statusLabel = xClusterConfig.paused ? PAUSED_LABEL : ENABLED_LABEL;
      break;
    case XClusterConfigStatus.FAILED:
      statusLabel = FAILED_LABEL;
      break;
    case XClusterConfigStatus.DELETION_FAILED:
      statusLabel = DELETION_FAILED_LABEL;
      break;
    case XClusterConfigStatus.DELETED_UNIVERSE: {
      const labelText =
        xClusterConfig.sourceUniverseUUID !== undefined &&
        xClusterConfig.targetUniverseUUID !== undefined
          ? 'Source/target universe deletion failed or in progress'
          : xClusterConfig.sourceUniverseUUID === undefined
          ? 'Source universe is deleted'
          : 'Target universe is deleted';
      statusLabel = (
        <span className={clsx(styles.label, styles.deleted)}>
          <i className="fa fa-exclamation-triangle" />
          {labelText}
        </span>
      );
      break;
    }
    case XClusterConfigStatus.DRAINED_DATA:
      statusLabel = DRAINED_DATA_LABEL;
      break;
    default:
      return assertUnreachableCase(xClusterConfig.status);
  }

  const tableCountsOfConcern = getTableCountsOfConcern(xClusterConfig.tableDetails);

  return (
    <div className={classes.pillContainer}>
      {statusLabel}
      {tableCountsOfConcern.uniqueTableCount === 0 && (
        <Typography variant="body2" className={clsx(pillClasses.pill, pillClasses.danger)}>
          {t('tablesOfConcernExist', { keyPrefix: 'clusterDetail.xCluster.shared' })}
        </Typography>
      )}
    </div>
  );
};
