/*
 * Created on Wed Jul 17 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { IBackupSchedule } from '../../../../../../components/backupv2';

export interface GeneralSettingsModel {
  scheduleName: IBackupSchedule['scheduleName'];
  storageConfig: {
    label: string;
    value: string;
    name: string;
    // regions are present only on multi-region storage condgigurations
    regions?: {
      REGION: string;
      LOCATION: string;
      AWS_HOST_BASE: string;
    }[];
  };
  // used only when YBC is not enabled on the universe
  parallelism?: number;
  isYBCEnabledInUniverse?: boolean;
}
