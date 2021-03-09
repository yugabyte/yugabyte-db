// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will help us to maintain the different type of configs
// along with their respective input field component except for AWS.

import { YBTextInputWithLabel } from "../../common/forms/fields";

const YBTextInputWithLabelComponent = YBTextInputWithLabel;
const StorageConfigTypes = {
  NFS: {
    title: "NFS Storage",
    fields: [
      {
        id: "NFS_CONFIGURATION_NAME",
        label: "Configuration Name",
        placeHolder: "Configuration Name",
        component: YBTextInputWithLabelComponent
      },
      {
        id: "NFS_BACKUP_LOCATION",
        label: "NFS Storage Path",
        placeHolder: "NFS Storage Path",
        component: YBTextInputWithLabelComponent
      }
    ]
  },
  GCS: {
    title: "GCS Storage",
    fields: [
      {
        id: "GCS_CONFIGURATION_NAME",
        label: "Configuration Name",
        placeHolder: "Configuration Name",
        component: YBTextInputWithLabelComponent
      },
      {
        id: "GCS_BACKUP_LOCATION",
        label: "GCS Bucket",
        placeHolder: "GCS Bucket",
        component: YBTextInputWithLabelComponent
      },
      {
        id: "GCS_CREDENTIALS_JSON",
        label: "GCS Credentials",
        placeHolder: "GCS Credentials JSON",
        component: YBTextInputWithLabelComponent
      }
    ]
  },
  AZ: {
    title: "Azure Storage",
    fields: [
      {
        id: "AZ_CONFIGURATION_NAME",
        label: "Configuration Name",
        placeHolder: "Configuration Name",
        component: YBTextInputWithLabelComponent
      },
      {
        id: "AZ_BACKUP_LOCATION",
        label: "Container URL",
        placeHolder: "Container URL",
        component: YBTextInputWithLabelComponent
      },
      {
        id: "AZURE_STORAGE_SAS_TOKEN",
        label: "SAS Token",
        placeHolder: "SAS Token",
        component: YBTextInputWithLabelComponent
      }
    ]
  }
};

export { StorageConfigTypes }