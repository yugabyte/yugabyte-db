import { TFunction } from 'i18next';
import { DeviceInfoValidationSchema } from './ValidationSchema';

const t = ((key: string) => key) as TFunction;

const onPremDeviceInfo = {
  volumeSize: 100,
  numVolumes: 2,
  storageClass: 'standard',
  storageType: null,
  diskIops: null,
  throughput: null
};

describe('DeviceInfoValidationSchema', () => {
  // On-prem mount points are joined from the instance type's volumeDetailsList.
  it.each([['/mnt/d0,/mnt/d1'], ['/mnt/d0, /mnt/data.1'], [null]])(
    'accepts mountPoints %s',
    async (mountPoints) => {
      await expect(
        DeviceInfoValidationSchema(t).validate({ ...onPremDeviceInfo, mountPoints })
      ).resolves.toMatchObject({ mountPoints });
    }
  );
});
