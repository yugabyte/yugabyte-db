import { useState } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useDispatch, useSelector } from 'react-redux';
import { find } from 'lodash';
import { toast } from 'react-toastify';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { AxiosError } from 'axios';

import { closeDialog, openDialog } from '../../../actions/modal';
import {
  editXClusterConfigTables,
  fetchTablesInUniverse,
  fetchTaskUntilItCompletes
} from '../../../actions/xClusterReplication';
import { formatSchemaName } from '../../../utils/Formatters';
import { YBButton } from '../../common/forms/fields';
import {
  formatBytes,
  CurrentTableReplicationLag,
  augmentTablesWithXClusterDetails
} from '../ReplicationUtils';
import DeleteReplicactionTableModal from './DeleteReplicactionTableModal';
import { ReplicationLagGraphModal } from './ReplicationLagGraphModal';
import { YBLabelWithIcon } from '../../common/descriptors';
import ellipsisIcon from '../../common/media/more.svg';
import { api, universeQueryKey, xClusterQueryKey } from '../../../redesign/helpers/api';
import {
  XClusterModalName,
  XClusterTableStatus,
  XCLUSTER_UNIVERSE_TABLE_FILTERS
} from '../constants';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { XClusterTableStatusLabel } from '../XClusterTableStatusLabel';
import { handleServerError } from '../../../utils/errorHandlingUtils';
import { AddTableModal } from './addTable/AddTableModal';
import {
  RbacValidator,
  hasNecessaryPerm
} from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { Action, Resource } from '../../../redesign/features/rbac';
import { getTableName, getTableUuid } from '../../../utils/tableUtils';

import { TableType, TableTypeLabel, YBTable } from '../../../redesign/helpers/dtos';
import { XClusterTable } from '../XClusterTypes';
import { XClusterConfig } from '../dtos';

import styles from './ReplicationTables.module.scss';

interface props {
  xClusterConfig: XClusterConfig;

  // isActive determines whether the component will make periodic
  // queries for metrics.
  isActive?: boolean;
  isDrInterface?: boolean;
}

const TABLE_MIN_PAGE_SIZE = 10;

export function ReplicationTables({
  xClusterConfig,
  isActive = true,
  isDrInterface = false
}: props) {
  const [deleteTableDetails, setDeleteTableDetails] = useState<XClusterTable>();
  const [openTableLagGraphDetails, setOpenTableLagGraphDetails] = useState<XClusterTable>();

  const dispatch = useDispatch();
  const { showModal, visibleModal } = useSelector((state: any) => state.modal);
  const queryClient = useQueryClient();

  const sourceUniverseTablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(xClusterConfig.sourceUniverseUUID, XCLUSTER_UNIVERSE_TABLE_FILTERS),
    () =>
      fetchTablesInUniverse(
        xClusterConfig.sourceUniverseUUID,
        XCLUSTER_UNIVERSE_TABLE_FILTERS
      ).then((respone) => respone.data)
  );

  const sourceUniverseQuery = useQuery(
    universeQueryKey.detail(xClusterConfig.sourceUniverseUUID),
    () => api.fetchUniverse(xClusterConfig.sourceUniverseUUID)
  );

  const removeTableFromXCluster = useMutation(
    (replication: XClusterConfig) => {
      return editXClusterConfigTables(replication.uuid, replication.tables);
    },
    {
      onSuccess: (response, xClusterConfig) => {
        fetchTaskUntilItCompletes(response.taskUUID, (err: boolean) => {
          if (!err) {
            queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfig.uuid));
            dispatch(closeDialog());
            toast.success(
              deleteTableDetails
                ? `"${getTableName(deleteTableDetails)}" table removed successfully from ${
                    xClusterConfig.name
                  }.`
                : `Table removed successfully from ${xClusterConfig.name}`
            );
          } else {
            toast.error(
              <span className="alertMsg">
                <i className="fa fa-exclamation-circle" />
                <span>
                  {deleteTableDetails
                    ? `Failed to remove table "${getTableName(deleteTableDetails)}" from ${
                        xClusterConfig.name
                      }.`
                    : `Failed to remove table from ${xClusterConfig.name}.`}
                </span>
                <a href={`/tasks/${response.taskUUID}`} target="_blank" rel="noopener noreferrer">
                  View Details
                </a>
              </span>
            );
          }
        });
      },
      onError: (error: Error | AxiosError) => {
        handleServerError(error, { customErrorLabel: 'Create xCluster config request failed' });
      }
    }
  );

  if (
    sourceUniverseTablesQuery.isLoading ||
    sourceUniverseTablesQuery.isIdle ||
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle
  ) {
    return <YBLoading />;
  }
  if (sourceUniverseTablesQuery.isError || sourceUniverseQuery.isError) {
    return <YBErrorIndicator />;
  }

  const showAddTablesToClusterModal = () => {
    dispatch(openDialog(XClusterModalName.ADD_TABLE_TO_CONFIG));
  };
  const hideModal = () => {
    dispatch(closeDialog());
  };

  const tablesInConfig = augmentTablesWithXClusterDetails(
    sourceUniverseTablesQuery.data,
    xClusterConfig.tableDetails
  );
  const sourceUniverse = sourceUniverseQuery.data;
  const isAddTableModalVisible =
    showModal && visibleModal === XClusterModalName.ADD_TABLE_TO_CONFIG;
  return (
    <div className={styles.rootContainer}>
      <div className={styles.headerSection}>
        <span className={styles.infoText}>Tables selected for Replication</span>
        <div className={styles.actionBar}>
          {!isDrInterface && (
            <RbacValidator
              customValidateFunction={(userPerm) => {
                return (
                  find(userPerm, {
                    resourceUUID: xClusterConfig.sourceUniverseUUID,
                    actions: [Action.BACKUP_RESTORE, Action.UPDATE],
                    resourceType: Resource.UNIVERSE
                  }) !== undefined &&
                  find(userPerm, {
                    resourceUUID: xClusterConfig.targetUniverseUUID,
                    actions: [Action.BACKUP_RESTORE, Action.UPDATE],
                    resourceType: Resource.UNIVERSE
                  }) !== undefined
                );
              }}
              isControl
            >
              <YBButton
                onClick={showAddTablesToClusterModal}
                btnIcon="fa fa-plus"
                btnText="Add Tables"
              />
            </RbacValidator>
          )}
        </div>
      </div>
      <div className={styles.replicationTable}>
        <BootstrapTable
          data={tablesInConfig}
          tableBodyClass={styles.table}
          trClassName="tr-row-style"
          pagination={tablesInConfig && tablesInConfig.length > TABLE_MIN_PAGE_SIZE}
        >
          <TableHeaderColumn dataField="tableUUID" isKey={true} hidden />
          <TableHeaderColumn
            dataField="tableName"
            dataFormat={(_, xClusterTable) => getTableName(xClusterTable)}
          >
            Table Name
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="pgSchemaName"
            dataFormat={(cell: string, row: YBTable) => formatSchemaName(row.tableType, cell)}
          >
            Schema Name
          </TableHeaderColumn>
          {!isDrInterface && (
            <TableHeaderColumn
              dataField="tableType"
              dataFormat={(cell: TableType) => TableTypeLabel[cell]}
            >
              Table Type
            </TableHeaderColumn>
          )}
          <TableHeaderColumn dataField="keySpace">Database</TableHeaderColumn>
          <TableHeaderColumn dataField="sizeBytes" dataFormat={(cell) => formatBytes(cell)}>
            Size
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="status"
            dataFormat={(cell: XClusterTableStatus, xClusterTable: XClusterTable) => (
              <XClusterTableStatusLabel
                status={cell}
                streamId={xClusterTable.streamId}
                sourceUniverseTableUuid={getTableUuid(xClusterTable)}
                sourceUniverseNodePrefix={sourceUniverse.universeDetails.nodePrefix}
                sourceUniverseUuid={sourceUniverse.universeUUID}
              />
            )}
          >
            Replication Status
          </TableHeaderColumn>
          <TableHeaderColumn
            dataFormat={(_, xClusterTable: XClusterTable) => (
              <span className="lag-text">
                <CurrentTableReplicationLag
                  streamId={xClusterTable.streamId}
                  tableId={getTableUuid(xClusterTable)}
                  nodePrefix={sourceUniverse.universeDetails.nodePrefix}
                  queryEnabled={isActive}
                  sourceUniverseUUID={xClusterConfig.sourceUniverseUUID}
                  xClusterConfigStatus={xClusterConfig.status}
                />
              </span>
            )}
          >
            Current lag
          </TableHeaderColumn>
          <TableHeaderColumn
            columnClassName={styles.tableActionColumn}
            width="160px"
            dataField="action"
            dataFormat={(_, row: XClusterTable) => (
              <>
                <YBButton
                  className={styles.actionButton}
                  btnIcon="fa fa-line-chart"
                  onClick={(e: any) => {
                    setOpenTableLagGraphDetails(row);
                    dispatch(openDialog(XClusterModalName.TABLE_REPLICATION_LAG_GRAPH));
                    e.currentTarget.blur();
                  }}
                />
                <Dropdown id={`${styles.tableActionColumn}_dropdown`} pullRight>
                  <Dropdown.Toggle noCaret className={styles.actionButton}>
                    <img src={ellipsisIcon} alt="more" className="ellipsis-icon" />
                  </Dropdown.Toggle>
                  <Dropdown.Menu>
                    <RbacValidator
                      customValidateFunction={() => {
                        return (
                          hasNecessaryPerm({
                            ...ApiPermissionMap.MODIFY_XLCUSTER_REPLICATION,
                            onResource: xClusterConfig.sourceUniverseUUID
                          }) &&
                          hasNecessaryPerm({
                            ...ApiPermissionMap.MODIFY_XLCUSTER_REPLICATION,
                            onResource: xClusterConfig.targetUniverseUUID
                          })
                        );
                      }}
                      isControl
                    >
                      <MenuItem
                        onClick={() => {
                          setDeleteTableDetails(row);
                          dispatch(openDialog(XClusterModalName.REMOVE_TABLE_FROM_CONFIG));
                        }}
                        disabled={row.tableType === TableType.TRANSACTION_STATUS_TABLE_TYPE}
                      >
                        <YBLabelWithIcon className={styles.dropdownMenuItem} icon="fa fa-times">
                          Remove Table
                        </YBLabelWithIcon>
                      </MenuItem>
                    </RbacValidator>
                  </Dropdown.Menu>
                </Dropdown>
              </>
            )}
          ></TableHeaderColumn>
        </BootstrapTable>
      </div>
      {isAddTableModalVisible && (
        <AddTableModal
          isDrInterface={isDrInterface}
          isVisible={isAddTableModalVisible}
          onHide={hideModal}
          xClusterConfig={xClusterConfig}
        />
      )}
      {openTableLagGraphDetails && (
        <ReplicationLagGraphModal
          tableDetails={openTableLagGraphDetails}
          replicationUUID={xClusterConfig.uuid}
          universeUUID={sourceUniverse.universeUUID}
          nodePrefix={sourceUniverse.universeDetails.nodePrefix}
          queryEnabled={isActive}
          visible={visibleModal === XClusterModalName.TABLE_REPLICATION_LAG_GRAPH}
          onHide={hideModal}
        />
      )}
      <DeleteReplicactionTableModal
        deleteTableName={deleteTableDetails ? getTableName(deleteTableDetails) : ''}
        onConfirm={() => {
          removeTableFromXCluster.mutate({
            ...xClusterConfig,
            tables: xClusterConfig.tables.filter((t) => t !== deleteTableDetails?.tableUUID)
          });
        }}
        onCancel={hideModal}
      />
    </div>
  );
}
