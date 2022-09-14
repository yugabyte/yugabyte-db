import React from 'react';
import clsx from 'clsx';

import { ReplicationStatus } from './constants';
import { Replication } from './XClusterTypes';

import styles from './XClusterConfigStatusLabel.module.scss';

interface XClusterConfigStatusProps {
  xClusterConfig: Replication;
}

export const XClusterConfigStatusLabel = ({ xClusterConfig }: XClusterConfigStatusProps) => {
  switch (xClusterConfig.status) {
    case ReplicationStatus.INITIALIZED:
    case ReplicationStatus.UPDATING:
      return (
        <span className={clsx(styles.label, styles.inProgress)}>
          <i className="fa fa-spinner fa-spin" />
          In Progress
        </span>
      );
    case ReplicationStatus.RUNNING:
      return xClusterConfig.paused ? (
        <span className={clsx(styles.label, styles.paused)}>
          <i className="fa fa-pause-circle-o" />
          Paused
        </span>
      ) : (
        <span className={clsx(styles.label, styles.running)}>
          <i className="fa fa-check-circle" />
          Enabled
        </span>
      );
    case ReplicationStatus.FAILED:
      return (
        <span className={clsx(styles.label, styles.failed)}>
          <i className="fa fa-exclamation-triangle" />
          Failed
        </span>
      );
    case ReplicationStatus.DELETION_FAILED:
      return (
        <span className={clsx(styles.label, styles.deleted)}>
          <i className="fa fa-close" />
          Deleted
        </span>
      );
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
  }
};
