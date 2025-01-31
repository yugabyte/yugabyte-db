/*
 * Created on Wed May 15 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { UniverseDetails } from '../../../universe/universe-form/utils/dto';
import { Task } from '../../dtos';

export enum DiffOperation {
  ADDED = 'Added',
  REMOVED = 'Removed',
  CHANGED = 'Changed',
  UNCHANGED = 'Unchanged'
}

export interface DiffProps {
  beforeData: unknown;
  afterData: unknown;
  operation?: DiffOperation;
  attribute?: string;
}

export interface DiffComponentProps extends DiffProps {
  task: Task;
}

// Represents the response from the diff API.
export interface DiffApiResp extends DiffProps {
  uuid: string;
  parentUuid: string;
}

export type GFlagDiff = {
  name: string;
  old: string | null;
  new: string | null;
  default: string;
};
export interface GFlagsDiffProps {
  gflags: {
    master: GFlagDiff[];
    tserver: GFlagDiff[];
  };
}
export interface AuditLogProps {
  customerUUID: string;
  payload: UniverseDetails;
  additionalDetails: UniverseDetails | GFlagsDiffProps;
  target: string;
  targetID: string;
  action: string;
  taskUUID: string;
  auditID: number;
}
