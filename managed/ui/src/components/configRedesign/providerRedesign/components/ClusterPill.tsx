import clsx from 'clsx';

import { assertUnreachableCase } from '../../../../utils/errorHandlingUtils';

import { Cluster, ClusterType } from '../../../../redesign/helpers/dtos';

import styles from './ClusterPill.module.scss';

interface ClusterPillProps {
  cluster: Cluster;
}

export const ClusterPill = ({ cluster }: ClusterPillProps) => {
  switch (cluster.clusterType) {
    case ClusterType.PRIMARY:
      return <div className={clsx(styles.pill)}>Primary</div>;
    case ClusterType.ASYNC:
      return <div className={clsx(styles.pill)}>Read Only</div>;
    default:
      return assertUnreachableCase(cluster.clusterType);
  }
};
