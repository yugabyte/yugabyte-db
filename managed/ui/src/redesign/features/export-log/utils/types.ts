import { Universe, Cluster } from '../../universe/universe-form/utils/dto';
import { GCPServiceAccount } from '../../../../components/configRedesign/providerRedesign/types';

export enum TelemetryProviderType {
  DATA_DOG = 'DATA_DOG',
  SPLUNK = 'SPLUNK',
  AWS_CLOUDWATCH = 'AWS_CLOUDWATCH',
  GCP_CLOUD_MONITORING = 'GCP_CLOUD_MONITORING'
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
    awsAccessKeyID?: string;
    awsAccessKeySecret?: string;
    logGroup?: string;
    logStream?: string;
    region?: string;
    roleARN?: string;
    //GCP CLOUD MONITOR
    project?: string;
    gcpCredentials?: File;
    credentials?: GCPServiceAccount;
  };
}

export interface ExportLogPayload extends ExportLogFormFields {
  tags?: {
    audit_user: string;
  };
}

export interface ExportLogResponse extends ExportLogPayload {
  uuid: string;
  customerUUID: string;
}

export interface UniverseItem extends Universe {
  linkedClusters: Cluster[];
}

export interface TPItem extends ExportLogPayload {
  linkedUniverses: UniverseItem[];
}
