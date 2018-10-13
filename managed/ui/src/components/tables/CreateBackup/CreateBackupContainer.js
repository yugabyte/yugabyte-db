// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { CreateBackup } from '../';
import { createTableBackup, createTableBackupResponse } from '../../../actions/tables';
import { reduxForm } from 'redux-form';

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
  const { customer: { configs } } = state;
  const storageConfigs = configs.data.filter( (config) => config.type === "STORAGE");
  return {
    storageConfigs: storageConfigs,
    universeDetails: state.universe.currentUniverse.data.universeDetails,
    universeTables: state.tables.universeTablesList,
    initialFormValues: {}
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
