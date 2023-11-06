/*
 * Created on Tue Mar 01 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Component, Suspense, lazy } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';

const AccountLevelBackupComponent = lazy(() =>
  import('../components/backupv2/Account/AccountLevelBackup').then(({ AccountLevelBackup }) => ({
    default: AccountLevelBackup
  }))
);

export default class Backups extends Component {
  render() {
    return (
      <Suspense fallback={YBLoadingCircleIcon}>
        <div className="dashboard-container">
          <h2 className="content-title">Backups</h2>
          <AccountLevelBackupComponent />
        </div>
      </Suspense>
    );
  }
}
