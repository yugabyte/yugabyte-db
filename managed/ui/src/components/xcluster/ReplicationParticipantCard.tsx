import React from 'react';
import clsx from 'clsx';

import { YBLoadingCircleIcon } from '../common/indicators';
import { usePillStyles } from '../configRedesign/providerRedesign/utils';

import styles from './ReplicationParticipantCard.module.scss';

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
  const pillClasses = usePillStyles();
  return (
    <div
      className={clsx(styles.replicationParticipant, isCurrentUniverse && styles.isCurrentUniverse)}
    >
      {!isLoading ? (
        <>
          <div className={pillClasses.pill}>{isSource ? 'Source' : 'Target'}</div>
          <div className={styles.participantName}>{clusterName}</div>
        </>
      ) : (
        <YBLoadingCircleIcon />
      )}
    </div>
  );
};
