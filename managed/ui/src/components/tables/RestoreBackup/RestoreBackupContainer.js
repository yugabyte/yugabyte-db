// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { RestoreBackup } from '../';
import { restoreTableBackup, restoreTableBackupResponse } from '../../../actions/tables';
import { reduxForm } from 'redux-form';
import { isDefinedNotNull } from "../../../utils/ObjectUtils";

const mapDispatchToProps = (dispatch) => {
  return {
    restoreTableBackup: (universeUUID, backupUUID, payload) => {
      dispatch(restoreTableBackup(universeUUID, backupUUID, payload)).then((response) => {
        dispatch(restoreTableBackupResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state) {
  return {
    universeDetails: state.universe.currentUniverse.data.universeDetails,
    universeList: state.universe.universeList
  };
}

function validate(values) {
  const errors = {};
  let hasErrors = false;

  if (!isDefinedNotNull(values.restoreUniverseUUID)) {
    errors.restoreUniverseUUID = 'Restore to Universe is required.';
    hasErrors = true;
  }
  if (!isDefinedNotNull(values.restoreKeyspace)) {
    errors.restoreKeyspace = 'Restore to Keyspace is required.';
    hasErrors = true;
  }
  if (!isDefinedNotNull(values.restoreTableName)) {
    errors.restoreTableName = 'Restore to TableName is required.';
    hasErrors = true;
  }

  return hasErrors && errors;
}

const restoreBackupForm = reduxForm({
  form: 'restoreBackupForm',
  fields: ['restoreUniverseUUID', 'restoreKeyspace', 'restoreTableName'],
  validate
});

export default connect(mapStateToProps, mapDispatchToProps)(restoreBackupForm(RestoreBackup));
