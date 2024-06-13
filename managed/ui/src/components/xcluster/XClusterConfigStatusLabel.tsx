import clsx from 'clsx';

import { makeStyles } from '@material-ui/core';
import { XClusterConfigStatus } from './constants';
import { assertUnreachableCase } from '../../utils/errorHandlingUtils';

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

const useSelectStyles = makeStyles((theme) => ({
  pillContainer: {
    display: 'flex',
    gap: theme.spacing(1),
    marginTop: theme.spacing(0.5),
    marginLeft: 'auto',
    flexWrap: 'wrap'
  }
}));

export const XClusterConfigStatusLabel = ({ xClusterConfig }: XClusterConfigStatusProps) => {
  const statusLabel = [];
  const classes = usePillStyles();
  const selectClasses = useSelectStyles();

  switch (xClusterConfig.status) {
    case XClusterConfigStatus.INITIALIZED:
    case XClusterConfigStatus.UPDATING:
      statusLabel.push(IN_PROGRESS_LABEL);
      break;
    case XClusterConfigStatus.RUNNING:
      statusLabel.push(xClusterConfig.paused ? PAUSED_LABEL : ENABLED_LABEL);
      break;
    case XClusterConfigStatus.FAILED:
      statusLabel.push(FAILED_LABEL);
      break;
    case XClusterConfigStatus.DELETION_FAILED:
      statusLabel.push(DELETION_FAILED_LABEL);
      break;
    case XClusterConfigStatus.DELETED_UNIVERSE: {
      const labelText =
        xClusterConfig.sourceUniverseUUID !== undefined &&
        xClusterConfig.targetUniverseUUID !== undefined
          ? 'Source/target universe deletion failed or in progress'
          : xClusterConfig.sourceUniverseUUID === undefined
          ? 'Source universe is deleted'
          : 'Target universe is deleted';
      statusLabel.push(
        <span className={clsx(styles.label, styles.deleted)}>
          <i className="fa fa-exclamation-triangle" />
          {labelText}
        </span>
      );
      break;
    }
    default:
      return assertUnreachableCase(xClusterConfig.status);
  }

  const replicationErrors: string[] = xClusterConfig.tableDetails
    .flatMap((table) => table.replicationStatusErrors)
    .filter((x, i, a) => a.indexOf(x) === i);

  if (replicationErrors.length !== 0) {
    statusLabel.push(
      <div className={selectClasses.pillContainer}>
        {replicationErrors.map((error, _) => {
          return (
            <div className={clsx(classes.pill, classes.danger)}>
              {error}
              <i className="fa fa-exclamation-circle" />
            </div>
          );
        })}
      </div>
    );
  }

  return <div>{statusLabel}</div>;
};
