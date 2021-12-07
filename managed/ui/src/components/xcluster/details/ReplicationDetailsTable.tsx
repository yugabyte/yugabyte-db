import React from 'react';
import { Col, Row } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useQuery } from 'react-query';
import { useDispatch } from 'react-redux';
import { openDialog } from '../../../actions/modal';
import { fetchTablesInUniverse, getUniverseInfo } from '../../../actions/xClusterReplication';
import { YBButton } from '../../common/forms/fields';
import { IReplication, IReplicationTable } from '../IClusterReplication';
import { GetCurrentLagForTable, YSQL_TABLE_TYPE } from '../ReplicationUtils';

import './ReplicationDetailsTable.scss';
interface props {
  replication: IReplication;
}

export function ReplicationDetailsTable({ replication }: props) {
  const dispatch = useDispatch();

  const showAddTablesToClusterModal = () => {
    dispatch(openDialog('addTablesToClusterModal'));
  };

  const { data: tablesInSourceUniverse, isLoading: isTablesLoading } = useQuery(
    [replication.sourceUniverseUUID, 'tables'],
    () => fetchTablesInUniverse(replication.sourceUniverseUUID).then((res) => res.data)
  );

  const { data: universeInfo, isLoading: currentUniverseLoading } = useQuery(
    ['universe', replication.sourceUniverseUUID],
    () => getUniverseInfo(replication.sourceUniverseUUID)
  );

  if (isTablesLoading || currentUniverseLoading) {
    return null;
  }

  const tablesInReplication = tablesInSourceUniverse
    .map((tables: IReplicationTable) => {
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
          {tablesInReplication.length} of {tablesInSourceUniverse.length} tables replicated
        </Col>
        <Col lg={6}>
          <div style={{ float: 'right' }}>
            <YBButton
              onClick={showAddTablesToClusterModal}
              btnText={
                <>
                  <i className="fa fa-plus" />
                  Modify Tables
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
              <TableHeaderColumn dataField="tableName" width="50%">
                Name
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="tableType"
                width="20%"
                dataFormat={(cell) => {
                  if (cell === YSQL_TABLE_TYPE) return 'YSQL';
                  return 'YCQL';
                }}
              >
                Type
              </TableHeaderColumn>
              <TableHeaderColumn dataField="keySpace" width="20%">
                Keyspace
              </TableHeaderColumn>
              <TableHeaderColumn dataField="sizeBytes" width="10%">
                Size
              </TableHeaderColumn>
              <TableHeaderColumn
                dataFormat={(_cell, row) => (
                  <span className="lag-text">
                    <GetCurrentLagForTable
                      replicationUUID={replication.uuid}
                      tableName={row.tableName}
                      nodePrefix={universeInfo?.data.universeDetails.nodePrefix}
                      enabled={isActiveTab}
                    />
                  </span>
                )}
              >
                Current lag (ms)
              </TableHeaderColumn>
            </BootstrapTable>
          </div>
        </Col>
      </Row>
    </>
  );
}
