// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { StopBackup } from './StopBackup';

const mapDispatchToProps = (dispatch) => {
  return {
    stopBackup: (backupUUID) => {
      // Implement the method logic.
      console.log(backupUUID)
    }
  };
};

export default connect(null, mapDispatchToProps)(StopBackup);
