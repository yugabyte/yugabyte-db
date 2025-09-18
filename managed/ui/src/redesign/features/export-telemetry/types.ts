import { GCPServiceAccount } from '../../../components/configRedesign/providerRedesign/types';
import { UniverseItem } from '@app/components/configRedesign/providerRedesign/providerView/providerDetails/UniverseTable';

export enum TelemetryProviderType {
  DATA_DOG = 'DATA_DOG',
  SPLUNK = 'SPLUNK',
  AWS_CLOUDWATCH = 'AWS_CLOUDWATCH',
  GCP_CLOUD_MONITORING = 'GCP_CLOUD_MONITORING',
  LOKI = 'LOKI',
  DYNATRACE = 'DYNATRACE'
}

export interface ExportLogFormFields {
  name: string;
  config: {
    type: TelemetryProviderType;
    //DATA DOG
    site?: string;
    apiKey?: string;
    //SPLUNK
    endpoint?: string; //Shared between splunk and AWS
    token?: string;
    source?: string;
    sourceType?: string;
    index?: string;
    //AWS CLOUD WATCH
    accessKey?: string;
    secretKey?: string;
    logGroup?: string;
    logStream?: string;
    region?: string;
    roleARN?: string;
    //GCP CLOUD MONITOR
    project?: string;
    gcpCredentials?: File;
    credentials?: GCPServiceAccount;
    // Loki
    authType?: string;
    organizationID?: string;
    basicAuth?: BasicAuth;
  };
}

export interface BasicAuth {
  username?: string;
  password?: string;
}

export interface ExportLogPayload extends ExportLogFormFields {
  tags?: {
    audit_user: string;
  };
}

export interface TelemetryProviderItem extends ExportLogPayload {
  linkedUniverses: UniverseItem[];
}
