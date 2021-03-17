// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will return the set of storage type options.

import React from "react";
import { isNonEmptyArray, isNonEmptyObject } from "../../utils/ObjectUtils";

export const BackupStorageOptions = (storageConfigs) => {
  let configTypeList = <option />;
  const optGroups =
    storageConfigs
    && isNonEmptyArray(storageConfigs)
    && storageConfigs.reduce((val, indx) => {
      const configType = indx.name;
      val[configType]
        ? val[configType].push({
          "configName": indx.configName,
          "configUUID": indx.configUUID
        })
        : (val[configType] = [{
          "configName": indx.configName,
          "configUUID": indx.configUUID
        }]);
      return val;
    }, {});

  if (isNonEmptyObject(optGroups)) {
    configTypeList = Object.keys(optGroups).map(function (key, idx) {
      return (
        <optgroup label={(key + " " + "storage").toUpperCase()} key={key + idx}>
          {optGroups[key]
            .sort((a, b) => /\d+(?!\.)/.exec(a) - /\d+(?!\.)/.exec(b))
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
}
