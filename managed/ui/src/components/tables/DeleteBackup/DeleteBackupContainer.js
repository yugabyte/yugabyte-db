// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { DeleteBackup } from '../';
import {
  bulkDeleteBackup,
  deleteBackup,
  deleteBackupResponse
} from '../../../actions/tables';

const mapDispatchToProps = (dispatch) => {
  return {
    deleteBackup: (backupUUID) => {
      return dispatch(deleteBackup(backupUUID)).then((response) => {
        if (!response.error) {
          dispatch(deleteBackupResponse(response));
          return response.payload;
        }
        throw new Error(response.error);
      });
    },
    bulkDeleteBackup: (payload) => {
      return dispatch(bulkDeleteBackup(payload)).then((res) => {
        if (!res.error) {
          dispatch(deleteBackupResponse(res));
          return res.payload;
        }
        throw new Error(res.error);
      });
    },
  };
};

export default connect(null, mapDispatchToProps)(DeleteBackup);
