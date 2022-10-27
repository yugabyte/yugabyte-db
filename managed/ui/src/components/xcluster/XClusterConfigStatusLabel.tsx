import React from 'react';
import clsx from 'clsx';

import { ReplicationStatus } from './constants';
import { XClusterConfig } from './XClusterTypes';

import styles from './XClusterConfigStatusLabel.module.scss';
import { assertUnreachableCase } from '../../utils/ErrorUtils';

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

export const XClusterConfigStatusLabel = ({ xClusterConfig }: XClusterConfigStatusProps) => {
  switch (xClusterConfig.status) {
    case ReplicationStatus.INITIALIZED:
    case ReplicationStatus.UPDATING:
      return IN_PROGRESS_LABEL;
    case ReplicationStatus.RUNNING:
      return xClusterConfig.paused ? PAUSED_LABEL : ENABLED_LABEL;
    case ReplicationStatus.FAILED:
      return FAILED_LABEL;
    case ReplicationStatus.DELETION_FAILED:
      return DELETION_FAILED_LABEL;
    case ReplicationStatus.DELETED_UNIVERSE: {
      const labelText =
        xClusterConfig.sourceUniverseUUID !== null && xClusterConfig.targetUniverseUUID !== null
          ? 'Source/target universe deletion failed or in progress'
          : xClusterConfig.sourceUniverseUUID === null
          ? 'Source universe is deleted'
          : 'Target universe is deleted';
      return (
        <span className={clsx(styles.label, styles.deleted)}>
          <i className="fa fa-exclamation-triangle" />
          {labelText}
        </span>
      );
    }
    default:
      return assertUnreachableCase(xClusterConfig.status);
  }
};
