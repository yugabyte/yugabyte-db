import React, { useState } from 'react';
import { Col, Row } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { fetchTablesInUniverse, editXClusterTables } from '../../../actions/xClusterReplication';
import { YBModalForm } from '../../common/forms';
import { YBButton, YBInputField } from '../../common/forms/fields';
import { YBLoading } from '../../common/indicators';
import { IReplication, IReplicationTable } from '../IClusterReplication';
import { YSQL_TABLE_TYPE } from '../ReplicationUtils';

import './AddTableToClusterModal.scss';

interface Props {
  onHide: () => void;
  visible: boolean;
  replication: IReplication;
}

export function AddTablesToClusterModal({ visible, onHide, replication }: Props) {
  const { data: tables, isLoading: isTablesLoading } = useQuery(
    [replication.sourceUniverseUUID, 'tables'],
    () => fetchTablesInUniverse(replication.sourceUniverseUUID).then((res) => res.data)
  );

  const [searchText, setSearchText] = useState('');

  const queryClient = useQueryClient();

  const addTablesToXCluster = useMutation(
    (replication: IReplication) => {
      return editXClusterTables(replication);
    },
    {
      onSuccess: () => {
        queryClient.invalidateQueries(['Xcluster', replication.uuid]);
        onHide();
      },
      onError: (err: any) => {
        toast.error(err.response.data.error);
      }
    }
  );

  if (isTablesLoading) {
    return <YBLoading />;
  }

  const tablesInSourceUniverse = tables.map((tables: IReplicationTable) => {
    return {
      ...tables,
      tableUUID: tables.tableUUID.replaceAll('-', '')
    };
  });

  const [tablesInReplication, tablesNotInReplication] = tablesInSourceUniverse.reduce(
    (out: IReplicationTable[][], currentTable: IReplicationTable) => {
      if (replication.tables.includes(currentTable.tableUUID)) {
        out[0].push(currentTable);
      } else {
        out[1].push(currentTable);
      }
      return out;
    },
    [[], []]
  );

  const initialValues = {
    tablesInReplication,
    tablesNotInReplication
  };

  const addTablesToClusterFunc = async (values: any, setSubmitting: Function) => {
    try {
      await addTablesToXCluster.mutateAsync({
        ...replication,
        tables: values['tablesInReplication'].map((t: IReplicationTable) => t.tableUUID)
      });
    } catch {
      setSubmitting(false);
    }
  };

  return (
    <YBModalForm
      onHide={onHide}
      size="large"
      visible={visible}
      initialValues={initialValues}
      title="Add tables to the replication"
      submitLabel={'Apply Changes'}
      onFormSubmit={(values: any, { setSubmitting }: { setSubmitting: Function }) => {
        addTablesToClusterFunc(values, setSubmitting);
      }}
      footerAccessory={<YBButton btnText="Cancel" onClick={onHide} />}
      render={({ values, setFieldValue }: { values: any; setFieldValue: Function }) => (
        <div className="add-tables-to-cluster">
          <Row className="info-search">
            <Col lg={6} className="info">
              List of common tables across source and target universes
            </Col>
            <Col lg={6}>
              <YBInputField
                placeHolder="Search"
                onValueChanged={(value: string) => setSearchText(value)}
              />
            </Col>
          </Row>
          <Row>
            <Col lg={12}>
              <div className="replication-info">Tables not replicated</div>
              <BootstrapTable
                data={values['tablesNotInReplication'].filter((table: IReplicationTable) => {
                  if (!searchText) {
                    return true;
                  }
                  return table.tableName.toLowerCase().indexOf(searchText.toLowerCase()) !== -1;
                })}
                height="300"
                tableContainerClass="add-to-table-container"
                selectRow={{
                  mode: 'checkbox',
                  selected: [],
                  onSelect: (row: IReplicationTable) => {
                    setFieldValue('tablesInReplication', [...values['tablesInReplication'], row]);

                    setFieldValue('tablesNotInReplication', [
                      ...values['tablesNotInReplication'].filter(
                        (r: IReplicationTable) => r.tableUUID !== row.tableUUID
                      )
                    ]);
                  },
                  onSelectAll: (_isSelected: boolean, rows: IReplicationTable[]) => {
                    setFieldValue('tablesInReplication', [
                      ...values['tablesInReplication'],
                      ...rows
                    ]);

                    setFieldValue('tablesNotInReplication', []);
                    return true;
                  }
                }}
                trClassName={(row) => {
                  if (
                    tablesInReplication.some(
                      (t: IReplicationTable) => t.tableUUID === row.tableUUID
                    )
                  ) {
                    return 'removed-from-replication';
                  }
                  return '';
                }}
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
              </BootstrapTable>
            </Col>
          </Row>
          <Row>
            <Col lg={12}>
              <div className="replication-info">Tables replicated</div>
              <BootstrapTable
                data={values['tablesInReplication'].filter((table: IReplicationTable) => {
                  if (!searchText) {
                    return true;
                  }
                  return table.tableName.toLowerCase().indexOf(searchText.toLowerCase()) !== -1;
                })}
                height="300"
                tableContainerClass="add-to-table-container"
                selectRow={{
                  mode: 'checkbox',
                  selected: values['tablesInReplication'].map(
                    (t: IReplicationTable) => t.tableUUID
                  ),
                  onSelect: (row: IReplicationTable) => {
                    setFieldValue('tablesNotInReplication', [
                      ...values['tablesNotInReplication'],
                      row
                    ]);

                    setFieldValue('tablesInReplication', [
                      ...values['tablesInReplication'].filter(
                        (r: IReplicationTable) => r.tableUUID !== row.tableUUID
                      )
                    ]);
                  },
                  onSelectAll: (_isSelected: boolean, rows: IReplicationTable[]) => {
                    setFieldValue('tablesNotInReplication', [
                      ...values['tablesNotInReplication'],
                      ...rows
                    ]);

                    setFieldValue('tablesInReplication', []);
                    return true;
                  }
                }}
                trClassName={(row) => {
                  if (
                    tablesNotInReplication.some(
                      (t: IReplicationTable) => t.tableUUID === row.tableUUID
                    )
                  ) {
                    return 'added-to-replication';
                  }
                  return '';
                }}
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
              </BootstrapTable>
            </Col>
          </Row>
        </div>
      )}
    />
  );
}
