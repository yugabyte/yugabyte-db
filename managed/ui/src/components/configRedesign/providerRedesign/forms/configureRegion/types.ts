import { K8sAvailabilityZoneFormValues, K8sRegionField } from './ConfigureK8sRegionModal';
import { CloudVendorRegionField } from './ConfigureRegionModal';
import {
  ConfigureOnPremRegionFormValues,
  OnPremAvailabilityZoneFormValues
} from './ConfigureOnPremRegionModal';
import { ExposedAZProperties } from './ConfigureAvailabilityZoneField';

export type SupportedRegionField =
  | CloudVendorRegionField
  | K8sRegionField
  | ConfigureOnPremRegionFormValues;

export type SupportedAZField =
  | ExposedAZProperties
  | K8sAvailabilityZoneFormValues
  | OnPremAvailabilityZoneFormValues;
