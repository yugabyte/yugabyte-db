/*
 * Created on Mon Feb 20 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

export interface SystemVariables {
  name: string;
  description: string;
}
export interface CustomVariable {
  uuid?: string;
  name: string;
  possibleValues: string[];
  defaultValue: string;
}

export interface IAlertVariablesList {
  systemVariables: SystemVariables[];
  customVariables: CustomVariable[];
}
