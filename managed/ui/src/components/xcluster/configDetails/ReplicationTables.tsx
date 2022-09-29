import React, { useState } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useDispatch, useSelector } from 'react-redux';
import { toast } from 'react-toastify';
import { Dropdown, MenuItem } from 'react-bootstrap';

import { closeDialog, openDialog } from '../../../actions/modal';
import {
  editXClusterTables,
  fetchTablesInUniverse,
  fetchTaskUntilItCompletes
} from '../../../actions/xClusterReplication';
import { formatSchemaName } from '../../../utils/Formatters';
import { YBButton } from '../../common/forms/fields';
import { formatBytes, GetCurrentLagForTable } from '../ReplicationUtils';
import DeleteReplicactionTableModal from './DeleteReplicactionTableModal';
import { ReplicationLagGraphModal } from './ReplicationLagGraphModal';
import { YBLabelWithIcon } from '../../common/descriptors';
import ellipsisIcon from '../../common/media/more.svg';
import { api } from '../../../redesign/helpers/api';
import { XClusterModalName } from '../constants';

import { TableType, TABLE_TYPE_MAP } from '../../../redesign/helpers/dtos';
import { XClusterConfig, YBTable } from '../XClusterTypes';

import styles from './ReplicationTables.module.scss';

interface props {
  replication: XClusterConfig;
}

const TABLE_MIN_PAGE_SIZE = 10;

export function ReplicationTables({ replication }: props) {
  const dispatch = useDispatch();
  const queryClient = useQueryClient();
  const { visibleModal } = useSelector((state: any) => state.modal);

  const [deleteTableDetails, setDeleteTableDetails] = useState<YBTable>();
  const [openTableLagGraphDetails, setOpenTableLagGraphDetails] = useState<YBTable>();

  const showAddTablesToClusterModal = () => {
    dispatch(openDialog(XClusterModalName.ADD_TABLE_TO_CONFIG));
  };

  const { data: tablesInSourceUniverse, isLoading: isTablesLoading } = useQuery(
    ['xcluster', replication.sourceUniverseUUID, 'tables'],
    () => fetchTablesInUniverse(replication.sourceUniverseUUID).then((res) => res.data)
  );

  const { data: universeInfo, isLoading: currentUniverseLoading } = useQuery(
    ['universe', replication.sourceUniverseUUID],
    () => api.fetchUniverse(replication.sourceUniverseUUID)
  );

  const removeTableFromXCluster = useMutation(
    (replication: XClusterConfig) => {
      return editXClusterTables(replication);
    },
    {
      onSuccess: (resp, replication) => {
        fetchTaskUntilItCompletes(resp.data.taskUUID, (err: boolean) => {
          if (!err) {
            queryClient.invalidateQueries(['Xcluster', replication.uuid]);
            dispatch(closeDialog());
            toast.success(`"${deleteTableDetails?.tableName}" table removed successfully`);
          } else {
            toast.error(
              <span className="alertMsg">
                <i className="fa fa-exclamation-circle" />
                <span>Task Failed.</span>
                <a href={`/tasks/${resp.data.taskUUID}`} target="_blank" rel="noopener noreferrer">
                  View Details
                </a>
              </span>
            );
          }
        });
      },
      onError: (err: any) => {
        toast.error(err.response.data.error);
      }
    }
  );

  if (isTablesLoading || currentUniverseLoading) {
    return null;
  }

  const tablesInReplication = tablesInSourceUniverse
    ?.map((tables: YBTable) => {
      return {
        ...tables,
        tableUUID: tables.tableUUID.replaceAll('-', '')
      };
    })
    .filter((table: YBTable) => replication.tables.includes(table.tableUUID));

  const isActiveTab = window.location.search === '?tab=tables';

  return (
    <div className={styles.rootContainer}>
      <div className={styles.headerSection}>
        <span className={styles.infoText}>Tables selected for Replication</span>
        <div className={styles.actionBar}>
          <YBButton
            onClick={showAddTablesToClusterModal}
            btnIcon="fa fa-plus"
            btnText="Add Tables"
          />
        </div>
      </div>
      <div className={styles.replicationTable}>
        <BootstrapTable
          data={tablesInReplication}
          tableBodyClass={styles.table}
          trClassName="tr-row-style"
          pagination={tablesInReplication && tablesInReplication.length > TABLE_MIN_PAGE_SIZE}
        >
          <TableHeaderColumn dataField="tableUUID" isKey={true} hidden />
          <TableHeaderColumn dataField="tableName">Table Name</TableHeaderColumn>
          <TableHeaderColumn
            dataField="pgSchemaName"
            dataFormat={(cell: string, row: YBTable) => formatSchemaName(row.tableType, cell)}
          >
            Schema Name
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="tableType"
            dataFormat={(cell: TableType) => TABLE_TYPE_MAP[cell]}
          >
            Table Type
          </TableHeaderColumn>
          <TableHeaderColumn dataField="keySpace">Keyspace</TableHeaderColumn>
          <TableHeaderColumn dataField="sizeBytes" dataFormat={(cell) => formatBytes(cell)}>
            Size
          </TableHeaderColumn>
          <TableHeaderColumn
            dataFormat={(_cell, row) => (
              <span className="lag-text">
                <GetCurrentLagForTable
                  replicationUUID={replication.uuid}
                  tableUUID={row.tableUUID}
                  nodePrefix={universeInfo?.universeDetails.nodePrefix}
                  enabled={isActiveTab}
                  sourceUniverseUUID={replication.sourceUniverseUUID}
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
            dataFormat={(_, row) => (
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
                    <MenuItem
                      onClick={() => {
                        setDeleteTableDetails(row);
                        dispatch(openDialog(XClusterModalName.REMOVE_TABLE_FROM_CONFIG));
                      }}
                    >
                      <YBLabelWithIcon className={styles.dropdownMenuItem} icon="fa fa-times">
                        Remove Table
                      </YBLabelWithIcon>
                    </MenuItem>
                  </Dropdown.Menu>
                </Dropdown>
              </>
            )}
          ></TableHeaderColumn>
        </BootstrapTable>
      </div>
      {openTableLagGraphDetails && universeInfo && (
        <ReplicationLagGraphModal
          tableDetails={openTableLagGraphDetails}
          replicationUUID={replication.uuid}
          universeUUID={universeInfo.universeUUID}
          nodePrefix={universeInfo.universeDetails.nodePrefix}
          queryEnabled={isActiveTab}
          visible={visibleModal === XClusterModalName.TABLE_REPLICATION_LAG_GRAPH}
          onHide={() => {
            dispatch(closeDialog());
          }}
        />
      )}
      <DeleteReplicactionTableModal
        deleteTableName={deleteTableDetails?.tableName ?? ''}
        onConfirm={() => {
          removeTableFromXCluster.mutate({
            ...replication,
            tables: replication.tables.filter((t) => t !== deleteTableDetails!.tableUUID)
          });
        }}
        onCancel={() => {
          dispatch(closeDialog());
        }}
      />
    </div>
  );
}
