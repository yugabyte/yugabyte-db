// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will return the set of storage type options.

import React from 'react';
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

      val[configType] = [...val[configType] ?? [], currentConfig];
      return val;
    }, {});

  if (isNonEmptyObject(optGroups)) {
    configTypeList = Object.keys(optGroups).map((key, idx) => {
      return (
        <optgroup label={(`${key} storage`).toUpperCase()} key={key + idx}>
          {optGroups[key]
            .sort((a, b) => regex.exec(a) - regex.exec(b))
            .map((item, arrIdx) => (
              <option key={idx + arrIdx} value={item.configUUID}>
                {item.configName}
              </option>
            ))}
        </optgroup>
      );
    });
  }

  return configTypeList;
};
