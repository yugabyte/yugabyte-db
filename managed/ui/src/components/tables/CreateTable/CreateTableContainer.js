// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { CreateTable } from '../../tables';
import { reduxForm } from 'redux-form';
import { createUniverseTable, createUniverseTableFailure, createUniverseTableSuccess,
         fetchColumnTypes, fetchColumnTypesSuccess, fetchColumnTypesFailure } from '../../../actions/tables';

const mapDispatchToProps = (dispatch) => {
  return {
    submitCreateTable: (currentUniverse, values) => {
      var rowCounter = 1;
      var partitionKeyList = values.partitionKeyColumns.map(function(row, idx){
        return {"columnOrder": rowCounter ++, "name": row.name, "type": row.selected,
                "isPartitionKey": true, "isClusteringKey": false};
      });
      var clusteringColumnList = values.clusteringColumns.map(function(row, idx){
        return {"columnOrder": rowCounter ++, "name": row.name, "type": row.selected,
          "isPartitionKey": false, "isClusteringKey": true};
      });
      var otherColumnList = values.otherColumns.map(function(row, idx){
        return {"columnOrder": rowCounter ++, "name": row.name, "type": row.selected,
          "isPartitionKey": false, "isClusteringKey": false};
      });
      var tableDetails = partitionKeyList.concat(clusteringColumnList, otherColumnList);

      var payload = {};
      payload.cloud = currentUniverse.universeDetails.cloud;
      payload.universeUUID = currentUniverse.universeUUID;
      payload.tableName = values.tableName;
      payload.tableType = "YSQL_TABLE_TYPE";
      payload.tableDetails = {
        "tableName": values.tableName,
        "columns": tableDetails
      };
      var universeUUID = currentUniverse.universeUUID;

      dispatch(createUniverseTable(universeUUID, payload)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(createUniverseTableFailure(response.payload));
        } else {
          dispatch(createUniverseTableSuccess(response.payload));
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
    }
  }
}

var createTableForm = reduxForm({
  form: 'CreateTableForm'
})


function mapStateToProps(state) {
  return {
    universe: state.universe,
    tables: state.tables
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(createTableForm(CreateTable));
