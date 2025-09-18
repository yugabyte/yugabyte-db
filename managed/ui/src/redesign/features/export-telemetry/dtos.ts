import { TelemetryProviderType } from './types';

/**
 * TODO: Replace `ExportLogFormFields` from src/redesign/features/export-log/utils/types.ts with the types in
 * this file after creating separate TelemetryProviderConfig types for each provider. This will enable type checking
 * for the form fields (currently every field is marked optional).
 * https://yugabyte.atlassian.net/browse/PLAT-18407
 *
 * Source: managed/src/main/java/com/yugabyte/yw/models/helpers/telemetry/TelemetryProviderConfig.java
 */
export interface TelemetryProviderConfig {
  type: TelemetryProviderType;
  config: {};
}

/**
 * Source: managed/src/main/java/com/yugabyte/yw/models/TelemetryProvider.java
 */
export interface TelemetryProvider {
  uuid: string;
  customerUUID: string;
  name: string;
  config: TelemetryProviderConfig;
  tags: Record<string, string>;
  createTime: string;
  updateTime: string;
}
