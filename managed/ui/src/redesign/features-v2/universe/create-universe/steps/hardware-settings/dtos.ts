import { DeviceInfo, K8NodeSpec } from '@app/redesign/features/universe/universe-form/utils/dto';
import { ArchitectureType } from '../../helpers/constants';

export interface InstanceSettingProps {
  arch: ArchitectureType;
  imageBundleUUID: string | null;
  useSpotInstance: boolean;
  instanceType: string | null;
  masterInstanceType: string | null;
  deviceInfo: DeviceInfo | null;
  masterDeviceInfo: DeviceInfo | null;
  tserverK8SNodeResourceSpec?: K8NodeSpec | null;
  masterK8SNodeResourceSpec?: K8NodeSpec | null;
  keepMasterTserverSame?: boolean;
  enableEbsVolumeEncryption?: boolean;
  ebsKmsConfigUUID?: string | null;
}
