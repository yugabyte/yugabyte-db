// Copyright (c) YugaByte, Inc.
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
  },
  AZ: {
    title: 'Azure Storage',
    fields: [
      {
        id: 'AZ_CONFIGURATION_NAME',
        label: 'Configuration Name',
        placeHolder: 'Configuration Name',
        component: YBTextInputWithLabel
      },
      {
        id: 'AZ_BACKUP_LOCATION',
        label: 'Container URL',
        placeHolder: 'Container URL',
        component: YBTextInputWithLabel
      },
      {
        id: 'AZURE_STORAGE_SAS_TOKEN',
        label: 'SAS Token',
        placeHolder: 'SAS Token',
        component: YBTextInputWithLabel
      }
    ]
  }
};
