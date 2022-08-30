import React, { FC } from 'react';
import { YBModal } from '../../common/forms/fields';

import { ReplicationTable } from '../XClusterReplicationTypes';
import { TableLagGraph } from './TableLagGraph';

import styles from './ReplicationLagGraphModal.module.scss';

interface Props {
  tableDetails: ReplicationTable;
  replicationUUID: string;
  queryEnabled: boolean;
  universeUUID: string;
  nodePrefix: string;
  visible: boolean;
  onHide: () => void;
}

// TODO: rename to TableLagGraphModal
export const ReplicationLagGraphModal: FC<Props> = ({
  tableDetails,
  replicationUUID,
  universeUUID,
  queryEnabled,
  nodePrefix,
  visible,
  onHide
}) => {
  return (
    <YBModal
      title={`Replication Lag: ${tableDetails.pgSchemaName}.${tableDetails.tableName}`}
      dialogClassName={styles.modelDialog}
      visible={visible}
      onHide={onHide}
    >
      <TableLagGraph
        tableDetails={tableDetails}
        replicationUUID={replicationUUID}
        universeUUID={universeUUID}
        nodePrefix={nodePrefix}
        queryEnabled={queryEnabled}
      />
    </YBModal>
  );
};
