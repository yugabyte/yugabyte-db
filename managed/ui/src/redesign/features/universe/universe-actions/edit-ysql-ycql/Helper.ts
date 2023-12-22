//types and interfaces
export interface YSQLFormFields {
  enableYSQL?: boolean;
  enableYSQLAuth?: boolean;
  ysqlPassword?: string;
  ysqlConfirmPassword?: string;
  rotateYSQLPassword?: boolean;
  ysqlCurrentPassword?: string;
  ysqlNewPassword?: string;
  ysqlConfirmNewPassword?: string;
}

export interface YCQLFormFields {
  enableYCQL?: boolean;
  enableYCQLAuth?: boolean;
  ycqlPassword?: string;
  ycqlConfirmPassword?: string;
  rotateYCQLPassword?: boolean;
  ycqlCurrentPassword?: string;
  ycqlNewPassword?: string;
  ycqlConfirmNewPassword?: string;
  overridePorts?: boolean;
  yqlServerHttpPort?: number;
  yqlServerRpcPort?: number;
}

export interface YSQLFormPayload {
  enableYSQL: boolean;
  enableYSQLAuth: boolean;
  ysqlPassword: string;
  CommunicationPorts: {
    ysqlServerHttpPort: number;
    ysqlServerRpcPort: number;
  };
}

export interface YCQLFormPayload {
  enableYCQL: boolean;
  enableYCQLAuth: boolean;
  ycqlPassword: string;
  CommunicationPorts: {
    yqlServerHttpPort: number;
    yqlServerRpcPort: number;
  };
}

export interface RotatePasswordPayload {
  dbName: string;
  ysqlAdminUsername: string;
  ysqlAdminPassword: string;
  ysqlCurrAdminPassword: string;
  ycqlAdminUsername: string;
  ycqlCurrAdminPassword: string;
  ycqlAdminPassword: string;
}

//constants
export const DATABASE_NAME = 'yugabyte';

export const YSQL_USER_NAME = 'yugabyte';

export const YCQL_USER_NAME = 'cassandra';
