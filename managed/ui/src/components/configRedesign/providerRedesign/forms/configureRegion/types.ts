import { K8sRegionField } from './ConfigureK8sRegionModal';
import { CloudVendorRegionField } from './ConfigureRegionModal';
import { ConfigureOnPremRegionFormValues } from './ConfigureOnPremRegionModal';

export type SupportedRegionField =
  | CloudVendorRegionField
  | K8sRegionField
  | ConfigureOnPremRegionFormValues;
