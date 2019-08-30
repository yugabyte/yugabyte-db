// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { CreateBackup } from '../';
import { createTableBackup, createTableBackupResponse } from '../../../actions/tables';
import { isNonEmptyArray, isNonEmptyObject } from "utils/ObjectUtils";

const mapDispatchToProps = (dispatch) => {
  return {
    createTableBackup: (universeUUID, tableUUID, payload) => {
      Object.keys(payload).forEach((key) => { if (typeof payload[key] === 'string' || payload[key] instanceof String) payload[key] = payload[key].trim(); });
      dispatch(createTableBackup(universeUUID, tableUUID, payload)).then((response) => {
        dispatch(createTableBackupResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state, ownProps) {
  const { customer: { configs }, tables: { universeTablesList } } = state;
  const storageConfigs = configs.data.filter( (config) => config.type === "STORAGE");
  const initialFormValues = {};

  if (isNonEmptyObject(ownProps.tableInfo)) {
    initialFormValues.backupTableUUID = ownProps.tableInfo.tableID;
  } else if (isNonEmptyArray(universeTablesList.data)) {
    initialFormValues.backupTableUUID = universeTablesList.tableUUID;
  }

  if (isNonEmptyArray(storageConfigs)) {
    initialFormValues.storageConfigUUID = storageConfigs[0].configUUID;
  }

  return {
    storageConfigs: storageConfigs,
    universeDetails: state.universe.currentUniverse.data.universeDetails,
    universeTables: state.tables.universeTablesList,
    initialValues: initialFormValues
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(CreateBackup);
