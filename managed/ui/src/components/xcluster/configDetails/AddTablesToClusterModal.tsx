import React, { useState } from 'react';
import { Col, Row } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import {
  fetchTablesInUniverse,
  editXClusterTables,
  fetchTaskUntilItCompletes
} from '../../../actions/xClusterReplication';
import { formatSchemaName } from '../../../utils/Formatters';
import { YBModalForm } from '../../common/forms';
import { YBButton, YBInputField } from '../../common/forms/fields';
import { YBLoading } from '../../common/indicators';

import { TableType, TABLE_TYPE_MAP } from '../../../redesign/helpers/dtos';
import { XClusterConfig, YBTable } from '../XClusterTypes';

import './AddTableToClusterModal.scss';

interface Props {
  onHide: () => void;
  visible: boolean;
  replication: XClusterConfig;
}

export function AddTablesToClusterModal({ visible, onHide, replication }: Props) {
  const { data: tables, isLoading: isTablesLoading } = useQuery(
    ['xcluster', replication.sourceUniverseUUID, 'tables'],
    () => fetchTablesInUniverse(replication.sourceUniverseUUID).then((res) => res.data)
  );

  const [searchText, setSearchText] = useState('');

  const queryClient = useQueryClient();

  const [selectedTables, setSelectedTables] = useState<string[]>([]);

  const addTablesToXCluster = useMutation(
    (replication: XClusterConfig) => {
      return editXClusterTables(replication);
    },
    {
      onSuccess: (resp) => {
        onHide();
        fetchTaskUntilItCompletes(resp.data.taskUUID, (err: boolean) => {
          if (!err) {
            queryClient.invalidateQueries(['Xcluster', replication.uuid]);
          } else {
            toast.error(
              <span className="alertMsg">
                <i className="fa fa-exclamation-circle" />
                <span>Unable to add table.</span>
                <a href={`/tasks/${resp.data.taskUUID}`} target="_blank" rel="noopener noreferrer">
                  View Details
                </a>
              </span>
            );
            queryClient.invalidateQueries(['Xcluster', replication.uuid]);
          }
        });
      },
      onError: (err: any) => {
        toast.error(err.response.data.error);
      }
    }
  );

  if (isTablesLoading) {
    return <YBLoading />;
  }

  const tablesInSourceUniverse = tables?.map((tables: YBTable) => {
    return {
      ...tables,
      tableUUID: tables.tableUUID.replaceAll('-', '')
    };
  });

  const tablesNotInReplication = tablesInSourceUniverse?.filter(
    (t: YBTable) => !replication.tables.includes(t.tableUUID)
  );

  const initialValues = {
    tablesNotInReplication
  };

  const handleRowSelect = (row: YBTable, isSelected: boolean) => {
    if (isSelected) {
      setSelectedTables([...selectedTables, row.tableUUID]);
    } else {
      setSelectedTables([...selectedTables.filter((t) => t !== row.tableUUID)]);
    }
  };

  const handleSelectAll = (isSelected: boolean, row: YBTable[]) => {
    if (isSelected) {
      setSelectedTables([...row.map((t) => t.tableUUID)]);
    } else {
      setSelectedTables([]);
    }
    return true;
  };

  const addTablesToClusterFunc = async (setSubmitting: Function) => {
    const uniqueTables = new Set([...replication.tables, ...selectedTables]);
    try {
      await addTablesToXCluster.mutateAsync({
        ...replication,
        tables: Array.from(uniqueTables)
      });
    } catch {
      setSubmitting(false);
    }
  };

  const resetAndHide = () => {
    setSearchText('');
    onHide();
  };

  return (
    <YBModalForm
      onHide={resetAndHide}
      size="large"
      visible={visible}
      initialValues={initialValues}
      title="Add tables"
      submitLabel={'Apply Changes'}
      onFormSubmit={(_: any, { setSubmitting }: { setSubmitting: Function }) => {
        addTablesToClusterFunc(setSubmitting);
      }}
      footerAccessory={<YBButton btnText="Cancel" onClick={resetAndHide} />}
      render={({ values, setFieldValue }: { values: any; setFieldValue: Function }) => (
        <div className="add-tables-to-cluster">
          <Row className="info-search">
            <Col lg={12} className="info">
              List of tables not replicated
            </Col>
            <Col lg={12}>
              <YBInputField
                placeHolder="Search"
                onValueChanged={(value: string) => setSearchText(value)}
              />
            </Col>
          </Row>
          <Row>
            <Col lg={12}>
              <BootstrapTable
                data={values['tablesNotInReplication'].filter((table: YBTable) => {
                  if (!searchText) {
                    return true;
                  }
                  return table.tableName.toLowerCase().indexOf(searchText.toLowerCase()) !== -1;
                })}
                height="300"
                tableContainerClass="add-to-table-container"
                selectRow={{
                  mode: 'checkbox',
                  onSelect: handleRowSelect,
                  onSelectAll: handleSelectAll
                }}
              >
                <TableHeaderColumn dataField="tableUUID" hidden isKey={true} />
                <TableHeaderColumn dataField="tableName" width="30%">
                  Table Name
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="pgSchemaName"
                  width="20%"
                  dataFormat={(cell: string, row: YBTable) => formatSchemaName(row.tableType, cell)}
                >
                  Schema Name
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="tableType"
                  width="20%"
                  dataFormat={(cell: TableType) => TABLE_TYPE_MAP[cell]}
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
