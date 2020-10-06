// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { DeleteBackup } from '../';
import { deleteBackup, deleteBackupResponse } from '../../../actions/tables';

const mapDispatchToProps = (dispatch) => {
  return {
    deleteBackup: (backupUUID) => {
      dispatch(deleteBackup(backupUUID)).then((response) => {
        dispatch(deleteBackupResponse(response));
      });
    }
  };
};

export default connect(null, mapDispatchToProps)(DeleteBackup);
