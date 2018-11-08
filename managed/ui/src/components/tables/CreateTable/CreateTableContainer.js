// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { CreateTable } from '..';
import { reduxForm } from 'redux-form';
import { isDefinedNotNull } from '../../../utils/ObjectUtils';
import { createUniverseTable, createUniverseTableFailure, createUniverseTableSuccess,
  toggleTableView } from '../../../actions/tables';
import { fetchUniverseTasks, fetchUniverseTasksResponse, closeUniverseDialog } from '../../../actions/universe';
import { openDialog, closeDialog } from '../../../actions/modal';

const mapDispatchToProps = (dispatch) => {
  return {
    submitCreateTable: (currentUniverse, values) => {
      let rowCounter = 1;
      const partitionKeyList = values.partitionKeyColumns.map(function(row, idx){
        return {
          "columnOrder": rowCounter++,
          "name": row.name,
          "type": row.selected,
          "isPartitionKey": true,
          "isClusteringKey": false
        };
      });
      const clusteringColumnList = values.clusteringColumns.map(function(row, idx){
        return {
          "columnOrder": rowCounter++,
          "name": row.name,
          "type": row.selected,
          "sortOrder": row.sortOrder,
          "isPartitionKey": false,
          "isClusteringKey": true
        };
      });
      const otherColumnList = values.otherColumns.map(function(row, idx){
        return {
          "columnOrder": rowCounter++,
          "name": row.name,
          "type": row.selected,
          "keyType": (row.keyType) ? row.keyType : null,
          "valueType": (row.valueType) ? row.valueType : null,
          "isPartitionKey": false,
          "isClusteringKey": false
        };
      });
      const allColumns = partitionKeyList.concat(clusteringColumnList, otherColumnList);

      const payload = {};
      payload.universeUUID = currentUniverse.universeUUID;
      payload.tableName = values.tableName;
      payload.tableType = "YQL_TABLE_TYPE";
      payload.tableDetails = {
        "tableName": values.tableName,
        "keyspace": values.keyspace,
        "columns": allColumns,
        "ttlInSeconds": values.ttlInSeconds
      };
      const universeUUID = currentUniverse.universeUUID;
      dispatch(createUniverseTable(universeUUID, payload)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(createUniverseTableFailure(response.payload));
        } else {
          dispatch(createUniverseTableSuccess(response.payload));
          dispatch(fetchUniverseTasks(universeUUID))
            .then((response) => {
              dispatch(toggleTableView("list"));
              dispatch(fetchUniverseTasksResponse(response.payload));

            });
        }
      });
    },

    showListTables: () => {
      dispatch(toggleTableView("list"));
    },

    showCancelCreateModal: () => {
      dispatch(openDialog("cancelCreate"));
    },

    hideModal: () => {
      dispatch(closeDialog());
      dispatch(closeUniverseDialog());
    }
  };
};

const validate = (values, props) => {
  const {tables: {columnDataTypes: {collections}}} = props;
  const errors = {};
  errors.partitionKeyColumns = [];
  errors.clusteringColumns = [];
  errors.otherColumns = [];
  if (!isDefinedNotNull(values.keyspace)) {
    errors.keyspace = 'Keyspace is Required';
  }
  if (!isDefinedNotNull(values.tableName)) {
    errors.tableName = 'Table Name Is Required';
  }
  if (values.partitionKeyColumns && values.partitionKeyColumns.length) {
    values.partitionKeyColumns.forEach((paritionKeyCol, colIndex) => {
      if (!paritionKeyCol || !isDefinedNotNull(paritionKeyCol.name)) {
        errors.partitionKeyColumns[colIndex] = {name: 'Required'};
      }
    });
  }
  if (values.clusteringColumns && values.clusteringColumns.length) {
    values.clusteringColumns.forEach((clusteringCol, colIndex) => {
      if (!clusteringCol || !isDefinedNotNull(clusteringCol.name)) {
        errors.clusteringColumns[colIndex] = {name: 'Required'};
      }
    });
  }
  if (values.otherColumns && values.otherColumns.length) {
    values.otherColumns.forEach((otherCol, colIndex) => {
      if (!otherCol || !isDefinedNotNull(otherCol.name)) {
        errors.otherColumns[colIndex] = {name: 'Required'};
      } else if (collections.indexOf(otherCol.selected) > -1) {
        if (!isDefinedNotNull(otherCol.keyType) || otherCol.keyType === 'Key Type') {
          errors.otherColumns[colIndex] = {keyType: 'Required'};
        }
        if (otherCol.selected.toUpperCase() === 'MAP' && (!isDefinedNotNull(otherCol.valueType) || otherCol.valueType === 'Value Type')) {
          errors.otherColumns[colIndex] = {valueType: 'Required'};
        }
      }
    });
  }

  return errors;
};

const createTableForm = reduxForm({
  form: 'CreateTableForm',
  validate,
  fields: ['tableName', 'ttlInSeconds', 'partitionKeyColumns[]', 'clusteringColumns[]', 'otherColumns[]']
});


function mapStateToProps(state) {
  return {
    universe: state.universe,
    tables: state.tables
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(createTableForm(CreateTable));
