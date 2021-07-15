// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { toast } from 'react-toastify';
import { stopBackup, stopBackupResponse } from '../../../actions/tables';
import { StopBackup } from './StopBackup';

const mapDispatchToProps = (dispatch) => {
  return {
    stopBackup: (backupUUID) => {
      return dispatch(stopBackup(backupUUID)).then((response) => {
        if (response.error) {
          const errorMessage = response.payload?.response?.data?.error || response.payload.message;
          toast.error(errorMessage);
        } else {
          toast.success('Successfully stopped the backup!');
        }
        dispatch(stopBackupResponse(response.payload));
        return response.payload;
      });
    }
  };
};

export default connect(null, mapDispatchToProps)(StopBackup);
