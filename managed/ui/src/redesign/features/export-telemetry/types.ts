import { GCPServiceAccount } from '../../../components/configRedesign/providerRedesign/types';
import { Universe } from '@app/redesign/helpers/dtos';

export enum TelemetryProviderType {
  DATA_DOG = 'DATA_DOG',
  SPLUNK = 'SPLUNK',
  AWS_CLOUDWATCH = 'AWS_CLOUDWATCH',
  GCP_CLOUD_MONITORING = 'GCP_CLOUD_MONITORING',
  LOKI = 'LOKI',
  DYNATRACE = 'DYNATRACE',
  S3 = 'S3',
  OTLP = 'OTLP'
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
    // Dynatrace
    apiToken?: string;
  };
}

export interface BasicAuth {
  username?: string;
  password?: string;
}

export interface ExportLogPayload extends ExportLogFormFields {
  tags?: Record<string, string>;
}

export interface TelemetryProviderItem extends ExportLogPayload {
  linkedUniverses: {
    universesWithLogExporter: Universe[];
    universesWithMetricsExporter: Universe[];
  };

  uuid?: string;
}
