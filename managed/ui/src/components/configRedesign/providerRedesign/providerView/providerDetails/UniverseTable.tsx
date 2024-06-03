/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import clsx from 'clsx';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { Link } from 'react-router';

import { Cluster, Universe } from '../../../../../redesign/helpers/dtos';
import { getPrimaryCluster, getReadOnlyClusters } from '../../../../../utils/universeUtilsTyped';
import {
  getUniverseStatus,
  getUniverseStatusIcon
} from '../../../../universes/helpers/universeHelpers';
import { UniverseAlertBadge } from '../../../../universes/YBUniverseItem/UniverseAlertBadge';
import { ImageBundleDefaultTag, ImageBundleYBActiveTag, getImageBundleUsedByUniverse } from '../../components/linuxVersionCatalog/LinuxVersionUtils';
import { ClusterPill } from '../../components/ClusterPill';
import { YBProvider } from '../../types';
import { ImageBundleType } from '../../../../../redesign/features/universe/universe-form/utils/dto';

import styles from './UniverseTable.module.scss';

export interface UniverseItem extends Universe {
  linkedClusters: Cluster[];
}

interface UniverseTableProps {
  linkedUniverses: UniverseItem[];
  providerConfig: YBProvider;
}

export const UniverseTable = ({ linkedUniverses, providerConfig }: UniverseTableProps) => {
  return (
    <div className={styles.bootstrapTableContainer}>
      <BootstrapTable tableContainerClass={styles.bootstrapTable} data={linkedUniverses}>
        <TableHeaderColumn
          dataField="name"
          isKey={true}
          dataSort={true}
          dataFormat={formatUniverseName}
        >
          Universe
        </TableHeaderColumn>
        <TableHeaderColumn dataFormat={formatUniverseStatus}>Universe Status</TableHeaderColumn>
        <TableHeaderColumn dataFormat={(_, row) => {
          return formatLinuxVersion(row, providerConfig);
        }}>Linux Version</TableHeaderColumn>
        <TableHeaderColumn dataFormat={(_, row) => {
          return <div className={styles.alertBadge}><UniverseAlertBadge universeUUID={row.universeUUID} listView /></div>;
        }}
          dataAlign='center' />
        <TableHeaderColumn dataFormat={formatUniverseActions} />
      </BootstrapTable>
    </div>
  );
};

const formatUniverseName = (universeName: string, row: UniverseItem) => {
  const primaryCluster = getPrimaryCluster(row.linkedClusters);
  const readOnlyClusters = getReadOnlyClusters(row.linkedClusters);
  return (
    <div className={styles.universeNameContainer}>
      <span>{universeName}</span>
      {primaryCluster && <ClusterPill cluster={primaryCluster} />}
      {readOnlyClusters && readOnlyClusters[0] && <ClusterPill cluster={readOnlyClusters[0]} />}
    </div>
  );
};

const formatUniverseStatus = (_: unknown, row: UniverseItem) => {
  const { state } = getUniverseStatus(row);
  return (
    <div className={clsx(styles.universeStatusCell, styles[state.className])}>
      <div>
        {getUniverseStatusIcon(state)}
        <span>{state.text}</span>
      </div>
    </div>
  );
};

const formatUniverseActions = (_: unknown, row: UniverseItem) => (
  <Link to={`/universes/${row.universeUUID}`}>Open Universe</Link>
);

const formatLinuxVersion = (row: UniverseItem, providerConfig: YBProvider) => {
  const imageBundle = getImageBundleUsedByUniverse(row.universeDetails, [providerConfig]);
  if (!imageBundle) return '';
  return <div className={styles.universeLinuxVersion}>
    {imageBundle.name.length > 20 ? `${imageBundle.name.substring(0, 20)}...` : imageBundle.name}
    {imageBundle.metadata?.type === ImageBundleType.YBA_ACTIVE && (<ImageBundleYBActiveTag />)}
    {imageBundle.useAsDefault && <ImageBundleDefaultTag />}
  </div>;
};
