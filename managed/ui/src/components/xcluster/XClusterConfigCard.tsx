import React from 'react';
import { useQuery } from 'react-query';
import { Link } from 'react-router';
import _ from 'lodash';
import clsx from 'clsx';

import { fetchUniversesList } from '../../actions/xClusterReplication';
import {
  convertToLocalTime,
  findUniverseName,
  GetConfiguredThreshold,
  GetCurrentLag
} from './ReplicationUtils';
import { XClusterConfigStatusLabel } from './XClusterConfigStatusLabel';
import { Replication } from './XClusterTypes';
import RightArrow from './ArrowIcon';
import { ReplicationParticipantCard } from './ReplicationParticipantCard';
import { ReplicationStatus } from './constants';

import styles from './XClusterConfigCard.module.scss';

interface XClusterConfigCardProps {
  xClusterConfig: Replication;
  currentUniverseUUID: string;
  currentUserTimezone: string | undefined;
}

export const XClusterConfigCard = ({
  xClusterConfig,
  currentUniverseUUID,
  currentUserTimezone
}: XClusterConfigCardProps) => {
  const universeListQuery = useQuery(['universeList'], () =>
    fetchUniversesList().then((res) => res.data)
  );

  const sourceUniverseName = !universeListQuery.isLoading
    ? findUniverseName(universeListQuery.data, xClusterConfig.sourceUniverseUUID)
    : '';
  const targetUniverseName = !universeListQuery.isLoading
    ? findUniverseName(universeListQuery.data, xClusterConfig.targetUniverseUUID)
    : '';

  return (
    <div className={styles.configCard}>
      <Link to={`/universes/${currentUniverseUUID}/replication/${xClusterConfig.uuid}`}>
        <div className={styles.headerSection}>
          <div className={styles.configName}>{xClusterConfig.name}</div>
          <div className={styles.metaInfoContainer}>
            <div className={styles.metaInfo}>
              <div className={styles.label}>Started</div>
              <div>{convertToLocalTime(xClusterConfig.createTime, currentUserTimezone)}</div>
            </div>
            <div className={styles.metaInfo}>
              <div className={styles.label}>Last modified</div>
              <div>{convertToLocalTime(xClusterConfig.modifyTime, currentUserTimezone)}</div>
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
          />
          <div className={styles.arrowIcon}>
            <RightArrow />
          </div>
          <ReplicationParticipantCard
            isSource={false}
            clusterName={targetUniverseName}
            isCurrentUniverse={currentUniverseUUID === xClusterConfig.targetUniverseUUID}
            isLoading={universeListQuery.isLoading}
          />
        </div>
        {_.includes(
          [ReplicationStatus.FAILED, ReplicationStatus.INITIALIZED, ReplicationStatus.UPDATING],
          xClusterConfig.status
        ) ? (
          <div className={styles.viewTasksPrompt}>
            <span>View progress on </span>
            <a href={`/universes/${xClusterConfig.targetUniverseUUID}/tasks`}>Tasks</a>.
          </div>
        ) : (
          <div className={styles.configMetricsContainer}>
            <div className={clsx(styles.configMetric, styles.maxAcceptableLag)}>
              <div className={styles.label}>Max acceptable lag</div>
              <div className={styles.value}>
                <GetConfiguredThreshold currentUniverseUUID={currentUniverseUUID} />
              </div>
            </div>
            <div className={clsx(styles.configMetric, styles.currentLag)}>
              <div className={styles.label}>Current Lag</div>
              <div className={styles.value}>
                <GetCurrentLag
                  replicationUUID={xClusterConfig.uuid}
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
