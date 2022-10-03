import React from 'react';
import clsx from 'clsx';

import styles from './ReplicationParticipantCard.module.scss';
import { YBLoadingCircleIcon } from '../common/indicators';

interface ReplicationParticipantCardProps {
  clusterName: string;
  isCurrentUniverse: boolean;
  isLoading: boolean;
  isSource: boolean;
}

export const ReplicationParticipantCard = ({
  clusterName,
  isCurrentUniverse,
  isLoading,
  isSource
}: ReplicationParticipantCardProps) => {
  return (
    <div
      className={clsx(styles.replicationParticipant, isCurrentUniverse && styles.isCurrentUniverse)}
    >
      {!isLoading ? (
        <>
          <div className={styles.label}>{isSource ? 'Source' : 'Target'}</div>
          <div className={styles.participantName}>{clusterName}</div>
        </>
      ) : (
        <YBLoadingCircleIcon />
      )}
    </div>
  );
};
