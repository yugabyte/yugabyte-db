import { useQuery } from 'react-query';
import { Link } from 'react-router';
import _ from 'lodash';
import clsx from 'clsx';

import RightArrow from './ArrowIcon';
import { ReplicationParticipantCard } from './ReplicationParticipantCard';
import { XClusterConfig } from './XClusterTypes';
import { XClusterConfigStatus, XClusterConfigTypeLabel } from './constants';
import { XClusterConfigStatusLabel } from './XClusterConfigStatusLabel';
import { fetchUniversesList } from '../../actions/xClusterReplication';
import { findUniverseName, MaxAcceptableLag, CurrentReplicationLag } from './ReplicationUtils';
import { usePillStyles } from '../configRedesign/providerRedesign/utils';
import { ybFormatDate } from '../../redesign/helpers/DateUtils';

import styles from './XClusterConfigCard.module.scss';

interface XClusterConfigCardProps {
  xClusterConfig: XClusterConfig;
  currentUniverseUUID: string;
}

export const XClusterConfigCard = ({
  xClusterConfig,
  currentUniverseUUID
}: XClusterConfigCardProps) => {
  const pillClasses = usePillStyles();
  const universeListQuery = useQuery(['universeList'], () =>
    fetchUniversesList().then((res) => res.data)
  );

  const sourceUniverseName =
    !universeListQuery.isLoading && xClusterConfig.sourceUniverseUUID
      ? findUniverseName(universeListQuery.data, xClusterConfig.sourceUniverseUUID)
      : '';
  const targetUniverseName =
    !universeListQuery.isLoading && xClusterConfig.targetUniverseUUID
      ? findUniverseName(universeListQuery.data, xClusterConfig.targetUniverseUUID)
      : '';

  return (
    <div className={styles.configCard}>
      <Link to={`/universes/${currentUniverseUUID}/replication/${xClusterConfig.uuid}`}>
        <div className={styles.headerSection}>
          <div className={styles.configNameContainer}>
            <div className={styles.configName}>{xClusterConfig.name}</div>
            <div className={pillClasses.pill}>{XClusterConfigTypeLabel[xClusterConfig.type]}</div>
          </div>
          <div className={styles.metaInfoContainer}>
            <div className={styles.metaInfo}>
              <div className={styles.label}>Started</div>
              <div>{ybFormatDate(xClusterConfig.createTime)}</div>
            </div>
            <div className={styles.metaInfo}>
              <div className={styles.label}>Last modified</div>
              <div>{ybFormatDate(xClusterConfig.modifyTime)}</div>
            </div>
          </div>
          <div className={styles.status}>
            <XClusterConfigStatusLabel xClusterConfig={xClusterConfig} />
          </div>
        </div>
      </Link>
      <div className={styles.bodySection}>
        <div className={styles.replicationGraph}>
          <ReplicationParticipantCard
            isSource={true}
            clusterName={sourceUniverseName}
            isCurrentUniverse={currentUniverseUUID === xClusterConfig.sourceUniverseUUID}
            isLoading={universeListQuery.isLoading}
            isActive={xClusterConfig.sourceActive}
            xClusterConfigType={xClusterConfig.type}
          />
          <div className={styles.arrowIcon}>
            <RightArrow />
          </div>
          <ReplicationParticipantCard
            isSource={false}
            clusterName={targetUniverseName}
            isCurrentUniverse={currentUniverseUUID === xClusterConfig.targetUniverseUUID}
            isLoading={universeListQuery.isLoading}
            isActive={xClusterConfig.targetActive}
            xClusterConfigType={xClusterConfig.type}
          />
        </div>
        {_.includes(
          [
            XClusterConfigStatus.FAILED,
            XClusterConfigStatus.INITIALIZED,
            XClusterConfigStatus.UPDATING
          ],
          xClusterConfig.status
        ) ? (
          <div className={styles.viewTasksPrompt}>
            <span>View progress on </span>
            <a href={`/universes/${xClusterConfig.sourceUniverseUUID}/tasks`}>Tasks</a>.
          </div>
        ) : (
          <div className={styles.configMetricsContainer}>
            <div className={clsx(styles.configMetric, styles.maxAcceptableLag)}>
              <div className={styles.label}>Max acceptable lag</div>
              <div className={styles.value}>
                <MaxAcceptableLag currentUniverseUUID={currentUniverseUUID} />
              </div>
            </div>
            <div className={clsx(styles.configMetric, styles.currentLag)}>
              <div className={styles.label}>Current Lag</div>
              <div className={styles.value}>
                <CurrentReplicationLag
                  xClusterConfigUUID={xClusterConfig.uuid}
                  xClusterConfigStatus={xClusterConfig.status}
                  sourceUniverseUUID={xClusterConfig.sourceUniverseUUID}
                />
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  );
};
