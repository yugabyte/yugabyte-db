import React, { useState } from 'react';
import { Col, Row } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useDispatch } from 'react-redux';
import { toast } from 'react-toastify';
import { closeDialog, openDialog } from '../../../actions/modal';
import {
  editXClusterTables,
  fetchTablesInUniverse,
  fetchTaskUntilItCompletes,
  getUniverseInfo
} from '../../../actions/xClusterReplication';
import { formatSchemaName } from '../../../utils/Formatters';
import { YBButton } from '../../common/forms/fields';
import { formatBytes, GetCurrentLagForTable } from '../ReplicationUtils';
import DeleteReplicactionTableModal from './DeleteReplicactionTableModal';

import { TableType, TABLE_TYPE_MAP } from '../../../redesign/helpers/dtos';
import { IReplication, IReplicationTable } from '../IClusterReplication';

import './ReplicationDetailsTable.scss';

interface props {
  replication: IReplication;
}

export function ReplicationDetailsTable({ replication }: props) {
  const dispatch = useDispatch();
  const queryClient = useQueryClient();

  const [deleteTableDetails, setDeleteTableDetails] = useState<IReplicationTable>();

  const showAddTablesToClusterModal = () => {
    dispatch(openDialog('addTablesToClusterModal'));
  };

  const { data: tablesInSourceUniverse, isLoading: isTablesLoading } = useQuery(
    ['xcluster', replication.sourceUniverseUUID, 'tables'],
    () => fetchTablesInUniverse(replication.sourceUniverseUUID).then((res) => res.data)
  );

  const { data: universeInfo, isLoading: currentUniverseLoading } = useQuery(
    ['universe', replication.sourceUniverseUUID],
    () => getUniverseInfo(replication.sourceUniverseUUID)
  );

  const removeTableFromXCluster = useMutation(
    (replication: IReplication) => {
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
    ?.map((tables: IReplicationTable) => {
      return {
        ...tables,
        tableUUID: tables.tableUUID.replaceAll('-', '')
      };
    })
    .filter((table: IReplicationTable) => replication.tables.includes(table.tableUUID));

  const isActiveTab = window.location.search === '?tab=tables';

  return (
    <>
      <div className="replication-divider" />
      <Row>
        <Col lg={6}>
          {tablesInReplication?.length} of {tablesInSourceUniverse?.length} tables replicated
        </Col>
        <Col lg={6}>
          <div style={{ float: 'right' }}>
            <YBButton
              onClick={showAddTablesToClusterModal}
              btnText={
                <>
                  <i className="fa fa-plus" />
                  Add Tables
                </>
              }
            />
          </div>
        </Col>
      </Row>
      <div className="replication-divider" />
      <Row>
        <Col lg={12}>
          <div className="replication-table">
            <BootstrapTable
              data={tablesInReplication}
              height={'300'}
              tableContainerClass="add-to-table-container"
            >
              <TableHeaderColumn dataField="tableUUID" isKey={true} hidden />
              <TableHeaderColumn dataField="tableName" width="25%">
                Name
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="pgSchemaName"
                width="15%"
                dataFormat={(cell: string, row: IReplicationTable) =>
                  formatSchemaName(row.tableType, cell)
                }
              >
                Schema Name
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="tableType"
                width="15%"
                dataFormat={(cell: TableType) => TABLE_TYPE_MAP[cell]}
              >
                Type
              </TableHeaderColumn>
              <TableHeaderColumn dataField="keySpace" width="15%">
                Keyspace
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="sizeBytes"
                width="10%"
                dataFormat={(cell) => formatBytes(cell)}
              >
                Size
              </TableHeaderColumn>
              <TableHeaderColumn
                dataFormat={(_cell, row) => (
                  <span className="lag-text">
                    <GetCurrentLagForTable
                      replicationUUID={replication.uuid}
                      tableUUID={row.tableUUID}
                      nodePrefix={universeInfo?.data.universeDetails.nodePrefix}
                      enabled={isActiveTab}
                      sourceUniverseUUID={replication.sourceUniverseUUID}
                    />
                  </span>
                )}
              >
                Current lag
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="action"
                width="10%"
                dataFormat={(_, row) => (
                  <YBButton
                    btnText="Remove Table"
                    onClick={() => {
                      setDeleteTableDetails(row);
                      dispatch(openDialog('DeleteReplicationTableModal'));
                    }}
                  />
                )}
                thStyle={{
                  textAlign: 'center'
                }}
              >
                Action
              </TableHeaderColumn>
            </BootstrapTable>
          </div>
        </Col>
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
      </Row>
    </>
  );
}
