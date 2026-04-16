interface AlertConfigurationSchema {
  targetType: 'UNIVERSE'; //only universe is supported right now
  target: {
    all: boolean;
    uuids: string[];
  };
}

export enum MaintenanceWindowState {
  FINISHED = 'FINISHED',
  ACTIVE = 'ACTIVE',
  PENDING = 'PENDING'
}
export interface MaintenanceWindowSchema {
  uuid: string;
  name: string;
  description: string;
  createTime: Date;
  endTime: string;
  startTime: string;
  state: MaintenanceWindowState;
  alertConfigurationFilter: AlertConfigurationSchema;
  suppressHealthCheckNotificationsConfig: {
    suppressAllUniverses: boolean;
    universeUUIDSet: string[];
  };
}
