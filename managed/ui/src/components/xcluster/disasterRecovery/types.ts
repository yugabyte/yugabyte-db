import { XClusterConfig } from '../XClusterTypes';

export interface DrConfig {
  uuid: string;
  name: string;
  createTime: string;
  modifyTime: string;
  xClusterConfig: XClusterConfig;
}
