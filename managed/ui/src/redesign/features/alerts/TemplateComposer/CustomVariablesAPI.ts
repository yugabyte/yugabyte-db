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
import {
  CustomVariable,
  IAlertChannelTemplates,
  IAlertConfiguration,
  IAlertConfigurationList,
  IAlertVariablesList
} from './ICustomVariables';

export enum ALERT_TEMPLATES_QUERY_KEY {
  fetchAlertTemplateVariables = 'fetchAlertTemplateVariables',
  fetchAlertConfigurationList = 'fetchAlertConfigurationList',
  getAlertChannelTemplates = 'getAlertChannelTemplates'
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

export const fetchAlertConfigList = (payload: Record<string, any>) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.post<IAlertConfigurationList>(
    `${ROOT_URL}/customers/${cUUID}/alert_configurations/list`,
    { payload }
  );
};

export const setVariableValueForAlertconfig = (alertConfig: IAlertConfiguration) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.put<IAlertConfigurationList>(
    `${ROOT_URL}/customers/${cUUID}/alert_configurations/${alertConfig.uuid}`,
    alertConfig
  );
};

export const getAlertChannelTemplates = () => {
  const cUUID = localStorage.getItem('customerId');
  return axios.get<IAlertChannelTemplates[]>(
    `${ROOT_URL}/customers/${cUUID}/alert_channel_templates`
  );
};

export const createAlertChannelTemplates = (template: IAlertChannelTemplates) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.post(
    `${ROOT_URL}/customers/${cUUID}/alert_channel_templates/${template.type}`,
    template
  );
};


export const previewAlertNotification = (template: IAlertChannelTemplates, alertConfigUuid: string) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.post(
    `${ROOT_URL}/customers/${cUUID}/alert_notification_preview`,
    { alertChannelTemplates: { ...template }, alertConfigUuid }
  );
};

