import { TFunction } from 'i18next';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { DeviceInfoValidationSchema, InstanceSettingsValidationSchema } from './ValidationSchema';

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

describe('InstanceSettingsValidationSchema hidden fields', () => {
  const filled = {
    volumeSize: 70,
    numVolumes: 1,
    storageClass: '',
    storageType: null,
    diskIops: null,
    throughput: null
  };
  // dedicated universe seeded with keepMasterTserverSame=false, empty master hardware
  const values = {
    arch: null,
    useSpotInstance: false,
    instanceType: 'c5.large',
    masterInstanceType: null,
    deviceInfo: filled,
    masterDeviceInfo: { ...filled, volumeSize: null },
    keepMasterTserverSame: false
  };
  const validate = (
    provider: CloudType,
    viewMode: 'all' | 'tserverOnly' | 'masterOnly',
    editMode: boolean,
    v: Record<string, unknown> = values
  ) =>
    InstanceSettingsValidationSchema(t, false, provider, true, 32, viewMode, editMode).validate(v, {
      abortEarly: false
    });

  it('ignores hidden master fields, storageClass (VM) and arch in the T-Server edit modal', async () => {
    await expect(validate(CloudType.aws, 'tserverOnly', true)).resolves.toBeDefined();
  });

  it('ignores hidden tserver fields in the Master edit modal', async () => {
    const v = {
      ...values,
      instanceType: null,
      deviceInfo: { ...filled, volumeSize: null },
      masterInstanceType: 'c5.large',
      masterDeviceInfo: filled
    };
    await expect(validate(CloudType.aws, 'masterOnly', true, v)).resolves.toBeDefined();
  });

  it('still validates both sections in "all" mode', async () => {
    await expect(validate(CloudType.aws, 'all', true)).rejects.toMatchObject({
      errors: expect.arrayContaining(['validation.required'])
    });
  });

  it('ignores hidden EBS KMS checks in the Master edit modal only', async () => {
    const v = {
      ...values,
      instanceType: null,
      deviceInfo: { ...filled, volumeSize: null },
      masterInstanceType: 'c5.large',
      masterDeviceInfo: filled,
      enableEbsVolumeEncryption: true,
      ebsKmsConfigUUID: null
    };
    await expect(validate(CloudType.aws, 'masterOnly', true, v)).resolves.toBeDefined();
    await expect(
      validate(CloudType.aws, 'tserverOnly', true, {
        ...values,
        enableEbsVolumeEncryption: true,
        ebsKmsConfigUUID: null
      })
    ).rejects.toBeDefined();
  });

  it('still requires arch outside edit mode', async () => {
    await expect(validate(CloudType.aws, 'tserverOnly', false)).rejects.toBeDefined();
  });

  // storageClass is rendered only by K8VolumeInfoField with k8sOverrideEnabled (custom resources)
  it.each([
    [true, true, true],
    [true, false, false],
    [false, false, false]
  ])(
    'K8s custom resources=%s, override enabled=%s: storageClass blocks submit=%s',
    async (customResources, overrideEnabled, blocks) => {
      const v = {
        ...values,
        keepMasterTserverSame: true,
        instanceType: customResources ? null : 'c5.large',
        tserverK8SNodeResourceSpec: { memoryGib: 4, cpuCoreCount: 2 }
      };
      const result = InstanceSettingsValidationSchema(
        t,
        customResources,
        CloudType.kubernetes,
        false,
        32,
        'all',
        true,
        overrideEnabled
      ).validate(v, { abortEarly: false });
      if (blocks) await expect(result).rejects.toBeDefined();
      else await expect(result).resolves.toBeDefined();
    }
  );
});
