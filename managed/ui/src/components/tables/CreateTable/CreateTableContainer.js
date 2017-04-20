// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { CreateTable } from '../../tables';
import { reduxForm } from 'redux-form';
import { isDefinedNotNull } from '../../../utils/ObjectUtils';
import { createUniverseTable, createUniverseTableFailure, createUniverseTableSuccess,
         fetchColumnTypes, fetchColumnTypesSuccess, fetchColumnTypesFailure, toggleTableView }
        from '../../../actions/tables';
import { fetchUniverseTasks, fetchUniverseTasksSuccess, fetchUniverseTasksFailure, resetUniverseTasks}
        from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    submitCreateTable: (currentUniverse, values) => {
      var rowCounter = 1;
      var partitionKeyList = values.partitionKeyColumns.map(function(row, idx){
        return {"columnOrder": rowCounter ++,
                "name": row.name,
                "type": row.selected,
                "isPartitionKey": true,
                "isClusteringKey": false};
      });
      var clusteringColumnList = values.clusteringColumns.map(function(row, idx){
        return {"columnOrder": rowCounter ++,
                "name": row.name,
                "type": row.selected,
                "sortOrder": row.sortOrder,
                "isPartitionKey": false,
                "isClusteringKey": true};
      });
      var otherColumnList = values.otherColumns.map(function(row, idx){
        return {"columnOrder": rowCounter ++,
                "name": row.name,
                "type": row.selected,
                "isPartitionKey": false,
                "isClusteringKey": false};
      });
      var tableDetails = partitionKeyList.concat(clusteringColumnList, otherColumnList);

      var payload = {};
      payload.cloud = currentUniverse.universeDetails.cloud;
      payload.universeUUID = currentUniverse.universeUUID;
      payload.tableName = values.tableName;
      payload.tableType = "YQL_TABLE_TYPE";
      payload.tableDetails = {
        "tableName": values.tableName,
        "columns": tableDetails,
        "ttlInSeconds": values.ttlInSeconds
      };
      var universeUUID = currentUniverse.universeUUID;
      dispatch(createUniverseTable(universeUUID, payload)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(createUniverseTableFailure(response.payload));
        } else {
          dispatch(createUniverseTableSuccess(response.payload));
          dispatch(fetchUniverseTasks(universeUUID))
            .then((response) => {
              dispatch(toggleTableView("list"));
              if (!response.error) {
                dispatch(fetchUniverseTasksSuccess(response.payload));
              } else {
                dispatch(fetchUniverseTasksFailure(response.payload));
              }
            });
        }
      });
    },
    fetchTableColumnTypes: () => {
      dispatch(fetchColumnTypes()).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(fetchColumnTypesFailure(response.payload));
        } else {
          dispatch(fetchColumnTypesSuccess(response.payload));
        }
      });
    },
    showListTables: () => {
      dispatch(toggleTableView("list"));
    }
  }
};

const validate = values => {
  const errors = {};
  errors.partitionKeyColumns = [];
  errors.clusteringColumns = [];
  errors.otherColumns = [];
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
      }
    });
  }

  return errors;
};

var createTableForm = reduxForm({
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
