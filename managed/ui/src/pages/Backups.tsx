/*
 * Created on Tue Mar 01 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { Component } from 'react';
import { AccountLevelBackup } from '../components/backupv2';

export default class Backups extends Component {
  render() {
    return (
      <div className="dashboard-container">
        <h2 className="content-title">Backups</h2>
        <AccountLevelBackup />
      </div>
    );
  }
}
