/*
 * Created on Mon Jan 09 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { BackupEmpty } from '../components/BackupEmpty';

const UPLOAD_ICON = <i className="fa fa-upload backup-empty-icon" />;

export const RestoreEmpty = () => {
  return (
    <BackupEmpty>
      {UPLOAD_ICON}
      <div className="sub-text">Currently there are no restores to show</div>
    </BackupEmpty>
  );
};
