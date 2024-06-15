import { FC } from 'react';

import { YBModal } from '../../common/forms/fields';
import { TableLagGraph } from './TableLagGraph';
import { getTableName } from '../../../utils/tableUtils';

import { XClusterTable } from '../XClusterTypes';

import styles from './TableLagGraphModal.module.scss';

interface Props {
  tableDetails: XClusterTable;
  replicationUUID: string;
  queryEnabled: boolean;
  universeUUID: string;
  nodePrefix: string;
  visible: boolean;
  onHide: () => void;
}

export const TableLagGraphModal: FC<Props> = ({
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
      title={`Table Replication Lag`}
      dialogClassName={styles.modelDialog}
      visible={visible}
      onHide={onHide}
    >
      <p>
        {'Table: '}
        <b>{getTableName(tableDetails)}</b>
      </p>
      {tableDetails.pgSchemaName && (
        <p>
          {'Schema: '}
          <b>{tableDetails.pgSchemaName}</b>
        </p>
      )}
      <p>
        {'Keyspace: '}
        <b>{tableDetails.keySpace}</b>
      </p>
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
