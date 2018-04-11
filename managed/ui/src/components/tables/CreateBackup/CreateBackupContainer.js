// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { CreateBackup } from '../';
import { createTableBackup, createTableBackupResponse } from '../../../actions/tables';
import { reduxForm } from 'redux-form';
import { isNonEmptyArray, isNonEmptyObject } from "../../../utils/ObjectUtils";

const mapDispatchToProps = (dispatch) => {
  return {
    createTableBackup: (universeUUID, tableUUID, payload) => {
      dispatch(createTableBackup(universeUUID, tableUUID, payload)).then((response) => {
        dispatch(createTableBackupResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state, ownProps) {
  const { customer: { configs } } = state;
  const { tableInfo } = ownProps;
  const storageConfigs = configs.data.filter( (config) => config.type === "STORAGE");
  let universeTables = [];
  if (isNonEmptyArray(state.tables.universeTablesList)) {
    universeTables = state.tables.universeTablesList.filter((tableInfo) => {
      return tableInfo.tableType !== "REDIS_TABLE_TYPE";
    });
  }

  const initialFormValues = {
    storageConfigUUID: null,
    backupTableUUID: null
  };
  if (isNonEmptyArray(storageConfigs)) {
    initialFormValues.storageConfigUUID = storageConfigs[0].configUUID;
  }
  // If the tableInfo object is not set, and we have tables for the universe
  // lets set the first tableUUID as the defaultUUID.
  if (!isNonEmptyObject(tableInfo) && isNonEmptyArray(universeTables)) {
    initialFormValues.backupTableUUID = universeTables[0].tableUUID;
  }

  return {
    customerConfigs: state.customer.configs,
    universeDetails: state.universe.currentUniverse.data.universeDetails,
    universeTables: universeTables,
    initialValues: initialFormValues
  };
}

function validate(values) {
  const errors = {};
  let hasErrors = false;
  if (!values.storageConfigUUID) {
    errors.storageConfigUUID = 'Storage Config is required.';
    hasErrors = true;
  }
  if (!values.backupTableUUID) {
    errors.backupTableUUID = 'Backup table is required.';
    hasErrors = true;
  }
  return hasErrors && errors;
}

const createBackupForm = reduxForm({
  form: 'CreateBackup',
  fields: ['storageConfigUUID', 'backupTableUUID'],
  validate
});

export default connect(mapStateToProps, mapDispatchToProps)(createBackupForm(CreateBackup));
