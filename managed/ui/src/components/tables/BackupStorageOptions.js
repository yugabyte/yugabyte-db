// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will return the set of storage type options.

import { isNonEmptyArray, isNonEmptyObject } from '../../utils/ObjectUtils';

export const BackupStorageOptions = (storageConfigs) => {
  let configTypeList = <option />;
  const regex = /\d+(?!\.)/;
  const optGroups =
    storageConfigs &&
    isNonEmptyArray(storageConfigs) &&
    storageConfigs.reduce((val, indx) => {
      const configType = indx.name;
      const currentConfig = {
        configName: indx.configName,
        configUUID: indx.configUUID
      };

      val[configType] = [...(val[configType] ?? []), currentConfig];
      return val;
    }, {});

  if (isNonEmptyObject(optGroups)) {
    configTypeList = Object.keys(optGroups).map((key, idx) => {
      return {
        label: `${key} storage`.toUpperCase(),
        options: optGroups[key]
          .sort((a, b) => regex.exec(a) - regex.exec(b))
          .map((item, arrIdx) => ({
            label: item.configName,
            value: item.configUUID,
            id: key.toUpperCase()
          }))
      };
    });
  }

  return configTypeList;
};
