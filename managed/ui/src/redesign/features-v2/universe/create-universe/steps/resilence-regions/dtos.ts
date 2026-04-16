import { Region } from '../../../../../features/universe/universe-form/utils/dto';
import {
  FAULT_TOLERANCE_TYPE,
  NODE_COUNT,
  REGIONS_FIELD,
  RESILIENCE_FACTOR,
  RESILIENCE_FORM_MODE,
  RESILIENCE_TYPE,
  SINGLE_AVAILABILITY_ZONE
} from '../../fields/FieldNames';

export enum ResilienceType {
  REGULAR = 'Regular',
  SINGLE_NODE = 'Single Node'
}
export enum ResilienceFormMode {
  GUIDED = 'Guided',
  EXPERT_MODE = 'Expert Mode'
}

export enum FaultToleranceType {
  REGION_LEVEL = 'REGION_LEVEL',
  AZ_LEVEL = 'AZ_LEVEL',
  NODE_LEVEL = 'NODE_LEVEL',
  NONE = 'NONE'
}

export interface ResilienceAndRegionsProps {
  [RESILIENCE_TYPE]: ResilienceType;
  [RESILIENCE_FORM_MODE]: ResilienceFormMode;
  [REGIONS_FIELD]: Region[];
  [RESILIENCE_FACTOR]: number;
  [FAULT_TOLERANCE_TYPE]: FaultToleranceType;
  [NODE_COUNT]: number;
  [SINGLE_AVAILABILITY_ZONE]?: string;
}
