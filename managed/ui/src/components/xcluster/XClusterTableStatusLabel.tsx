import React from 'react';
import clsx from 'clsx';

import { XClusterTableStatus } from './constants';

import styles from './XClusterTableStatusLabel.module.scss';

interface XClusterTableStatusProps {
  status: XClusterTableStatus;
}

export const XClusterTableStatusLabel = ({ status }: XClusterTableStatusProps) => {
  switch (status) {
    case XClusterTableStatus.OPERATIONAL:
      return (
        <span className={clsx(styles.label, styles.ready)}>
          Operational
          <i className="fa fa-check" />
        </span>
      );
    case XClusterTableStatus.WARNING:
      return (
        <span className={clsx(styles.label, styles.warning)}>
          Warning
          <i className="fa fa-exclamation-triangle" />
        </span>
      );
    case XClusterTableStatus.FAILED:
      return (
        <span className={clsx(styles.label, styles.error)}>
          Failed
          <i className="fa fa-exclamation-circle" />
        </span>
      );
    case XClusterTableStatus.ERROR:
      return (
        <span className={clsx(styles.label, styles.error)}>
          Error
          <i className="fa fa-exclamation-circle" />
        </span>
      );
    case XClusterTableStatus.IN_PROGRESS:
      return (
        <span className={clsx(styles.label, styles.inProgress)}>
          In Progress
          <i className="fa fa-spinner fa-spin" />
        </span>
      );
    case XClusterTableStatus.VALIDATING:
      return (
        <span className={clsx(styles.label, styles.inProgress)}>
          Validating
          <i className="fa fa-spinner fa-spin" />
        </span>
      );
    case XClusterTableStatus.BOOTSTRAPPING:
      return (
        <span className={clsx(styles.label, styles.inProgress)}>
          Bootstrapping
          <i className="fa fa-spinner fa-spin" />
        </span>
      );
  }
};
