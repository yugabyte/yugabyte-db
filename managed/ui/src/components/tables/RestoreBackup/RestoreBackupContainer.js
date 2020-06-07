// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { RestoreBackup } from '../';
import { restoreTableBackup, restoreTableBackupResponse } from '../../../actions/tables';
import { isNonEmptyArray, isNonEmptyObject } from "../../../utils/ObjectUtils";
import { getPromiseState } from '../../../utils/PromiseUtils';

const mapDispatchToProps = (dispatch) => {
  return {
    restoreTableBackup: (universeUUID, payload) => {
      dispatch(restoreTableBackup(universeUUID, payload)).then((response) => {
        dispatch(restoreTableBackupResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state, ownProps) {
  const initialFormValues = {
    restoreToUniverseUUID: '',
    restoreToTableName: '',
    restoreToKeyspace: '',
    storageConfigUUID: '',
    storageLocation: ''
  };
  const { customer: { configs }, universe: { currentUniverse, universeList} } = state;
  const storageConfigs = configs.data.filter( (config) => config.type === "STORAGE");

  if (isNonEmptyObject(ownProps.backupInfo)) {
    const { backupInfo : {
      storageConfigUUID, storageLocation, universeUUID, keyspace, tableName }
    } = ownProps;

    /* AC: Careful! This sets the default of the Select but the return value
     * is a string while the other options, when selected, return an object
     * with the format { label: <display>, value: <internal> }
     */
    initialFormValues.restoreToUniverseUUID = universeUUID;

    initialFormValues.restoreToTableName = tableName;
    initialFormValues.restoreToKeyspace = keyspace;
    initialFormValues.storageConfigUUID = storageConfigUUID;
    initialFormValues.storageLocation = storageLocation;
  } else {
    if (getPromiseState(currentUniverse).isSuccess() &&
        isNonEmptyObject(currentUniverse.data)) {
      initialFormValues.restoreToUniverseUUID = currentUniverse.data.universeUUID;
    }
    if (isNonEmptyArray(storageConfigs)) {
      initialFormValues.storageConfigUUID = storageConfigs[0].configUUID;
    }
  }

  return {
    storageConfigs: storageConfigs,
    currentUniverse: currentUniverse,
    universeList: universeList,
    initialValues: initialFormValues
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(RestoreBackup);
