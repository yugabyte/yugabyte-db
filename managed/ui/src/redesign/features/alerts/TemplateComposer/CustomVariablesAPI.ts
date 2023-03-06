/*
 * Created on Thu Feb 16 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from 'axios';
import { ROOT_URL } from '../../../../config';
import { CustomVariable, IAlertVariablesList } from './ICustomVariables';

export enum ALERT_TEMPLATES_QUERY_KEY {
  fetchAlertTemplateVariables = 'fetchAlertTemplateVariables'
}

export const fetchAlertTemplateVariables = () => {
  const cUUID = localStorage.getItem('customerId');
  return axios.get<IAlertVariablesList>(`${ROOT_URL}/customers/${cUUID}/alert_template_variables`);
};

export const createCustomAlertTemplteVariable = (variables: CustomVariable[]) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.put(`${ROOT_URL}/customers/${cUUID}/alert_template_variables`, {
    variables: variables.map((v) => {
      return {
        ...v,
        customerUUID: cUUID
      };
    })
  });
};

export const deleteCustomAlertTemplateVariable = (variable: CustomVariable) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.delete(`${ROOT_URL}/customers/${cUUID}/alert_template_variables/${variable.uuid}`);
};
