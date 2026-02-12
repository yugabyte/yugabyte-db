// Copyright (c) YugabyteDB, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will help us to maintain the different type of configs
// along with their respective input field component except for AWS.

import { YBTextInputWithLabel } from '../../common/forms/fields';

export const storageConfigTypes = {
  NFS: {
    title: 'NFS Storage',
    fields: [
      {
        id: 'NFS_CONFIGURATION_NAME',
        label: 'Configuration Name',
        placeHolder: 'Configuration Name',
        component: YBTextInputWithLabel
      },
      {
        id: 'NFS_BACKUP_LOCATION',
        label: 'NFS Storage Path',
        placeHolder: 'NFS Storage Path',
        component: YBTextInputWithLabel
      }
    ]
  }
};
