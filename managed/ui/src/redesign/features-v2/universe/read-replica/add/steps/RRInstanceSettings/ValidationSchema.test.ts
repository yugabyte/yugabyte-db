import { TFunction } from 'i18next';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { RRInstanceSettingsValidationSchema } from './ValidationSchema';

const t = ((key: string) => key) as TFunction;

describe('RRInstanceSettingsValidationSchema', () => {
  // API-created primaries store storageClass "", and VolumeInfoField has no input for it
  it('does not require storageClass for a VM read replica', async () => {
    await expect(
      RRInstanceSettingsValidationSchema(t, false, CloudType.aws).validate({
        inheritPrimaryInstance: false,
        instanceType: 'c5.large',
        deviceInfo: { volumeSize: 70, numVolumes: 1, storageClass: '', storageType: null }
      })
    ).resolves.toBeDefined();
  });
});
