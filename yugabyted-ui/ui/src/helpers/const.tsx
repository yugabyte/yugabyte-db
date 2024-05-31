import React from 'react';
import { CloudEnum } from '@app/api/src';
import config from '@app/config.json';
import AWSLogoIcon from '@app/assets/logo-aws.svg';
import GCPLogoIcon from '@app/assets/logo-gcp.svg';
import AzureLogoIcon from '@app/assets/logo-azure.svg';

export const IS_DEV_MODE = config.env === 'local';
export const IS_PROD_ENV = config.env === 'prod';

export const ALPHANUMERIC_REGEX = /^(?:[a-z](?:[-a-z0-9]{0,61}[a-z0-9])?)$/;

export const PASSWORD_REGEX = /^(?=.*[a-z])(?=.*[A-Z])[a-zA-Z0-9!\\"#$%&'()*+,-./:;<=>?@^_`{|}~]{8,256}$/;

export const PASSWORD_MIN_LENGTH = 8;

export const PASSWORD_MAX_LENGTH = 256;

export const CIDR_IP_REGEX = /^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5]))(\/(\d|[1-2]\d|3[0-2]))?$/;

export const CIDR_REGEX = /^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5]))(\/(\d|[1-2]\d|3[0-2]))$/;

export const IP_REGEX = /((\d+\.){3}\d+)/;

export const PHONE_REGEX = /^\s*(?:\+?(\d{1,3}))?[-. (]*(\d{3})[-. )]*(\d{3})[-. ]*(\d{4})(?: *x(\d+))?\s*$/;

export const AWS_ACCOUNT_ID_REGEX = /^\d{12}$/;
export const AWS_VPC_ID_REGEX = /^vpc-[^\s]+$/;

export const CHART_RESIZE_DEBOUNCE = 100;

export const clouds = [
  {
    value: CloudEnum.Aws,
    label: 'AWS',
    visible: true,
    icon: <AWSLogoIcon />
  },
  {
    value: CloudEnum.Gcp,
    label: 'GCP',
    visible: true,
    icon: <GCPLogoIcon />
  },
  {
    // value: 'azure', // TODO: replace by CloudEnum.Azure when supported
    label: 'Azure',
    visible: false,
    icon: <AzureLogoIcon />
  }
];

export const TEST_CAPTCHA_SITEKEY = '10000000-ffff-ffff-ffff-000000000001';

//TODO: Port all other links here
export const EXTERNAL_LINKS: Record<string, string> = {
  CONTACT_SUPPORT: 'https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431',
  NEW_RELEASES: 'https://docs.yugabyte.com/preview/releases/whats-new/',
  MANAGE_UPGRADE: 'https://docs.yugabyte.com/preview/manage/upgrade-deployment/',
  MANAGE_UPGRADE_MORE:
    'https://docs.yugabyte.com/preview/yugabyte-cloud/cloud-clusters/cloud-maintenance/#what-to-expect-during-maintenance'
};
