import clsx from 'clsx';

import { YBLoadingCircleIcon } from '../common/indicators';
import { usePillStyles } from '../../redesign/styles/styles';
import { XClusterConfigType } from './constants';
import { getIsTransactionalAtomicityEnabled } from './ReplicationUtils';

import styles from './ReplicationParticipantCard.module.scss';

interface ReplicationParticipantCardProps {
  clusterName: string;
  isActive: boolean;
  isCurrentUniverse: boolean;
  isLoading: boolean;
  isSource: boolean;
  xClusterConfigType: XClusterConfigType;
}

export const ReplicationParticipantCard = ({
  clusterName,
  isActive,
  isCurrentUniverse,
  isLoading,
  isSource,
  xClusterConfigType
}: ReplicationParticipantCardProps) => {
  const pillClasses = usePillStyles();
  return (
    <div
      className={clsx(styles.replicationParticipant, isCurrentUniverse && styles.isCurrentUniverse)}
    >
      {!isLoading ? (
        <>
          <div className={styles.pillContainer}>
            <div className={pillClasses.pill}>{isSource ? 'Source' : 'Target'}</div>
            {getIsTransactionalAtomicityEnabled(xClusterConfigType) && (
              <div className={pillClasses.pill}>{isActive ? 'Active' : 'Standby'}</div>
            )}
          </div>

          <div className={styles.participantName}>{clusterName}</div>
        </>
      ) : (
        <YBLoadingCircleIcon />
      )}
    </div>
  );
};
