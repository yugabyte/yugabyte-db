import { compareYBSoftwareVersions } from './universeUtilsTyped';

describe('YB Software version comparison function - compareYBSoftwareVersions', () => {
  it('handles software versions with build numbers', () => {
    expect(compareYBSoftwareVersions({ versionA: '0.0.0.0-b1', versionB: '0.0.0.0-b1' })).toEqual(0);
    expect(compareYBSoftwareVersions({ versionA: '2.17.3.0-b4', versionB: '2.17.3.0-b4' })).toEqual(0);
    expect(compareYBSoftwareVersions({ versionA: '1.303.505.44-b405', versionB: '1.303.505.44-b405' })).toEqual(0);

    expect(compareYBSoftwareVersions({ versionA: '3.17.3.0-b4', versionB: '2.17.3.0-b4' })).toBeGreaterThan(0);
    expect(compareYBSoftwareVersions({ versionA: '2.18.3.0-b4', versionB: '2.17.3.0-b4' })).toBeGreaterThan(0);
    expect(compareYBSoftwareVersions({ versionA: '2.17.4.0-b4', versionB: '2.17.3.0-b4' })).toBeGreaterThan(0);
    expect(compareYBSoftwareVersions({ versionA: '2.17.3.1-b4', versionB: '2.17.3.0-b4' })).toBeGreaterThan(0);
    expect(compareYBSoftwareVersions({ versionA: '2.17.3.0-b5', versionB: '2.17.3.0-b4' })).toBeGreaterThan(0);
    expect(compareYBSoftwareVersions({ versionA: '2.17.3.0-b205', versionB: '2.17.3.0-b4' })).toBeGreaterThan(0);

    expect(compareYBSoftwareVersions({ versionA: '2.17.3.0-b4', versionB: '3.17.3.0-b4' })).toBeLessThan(0);
    expect(compareYBSoftwareVersions({ versionA: '2.17.3.0-b4', versionB: '2.18.3.0-b4' })).toBeLessThan(0);
    expect(compareYBSoftwareVersions({ versionA: '2.17.3.0-b4', versionB: '2.17.4.0-b4' })).toBeLessThan(0);
    expect(compareYBSoftwareVersions({ versionA: '2.17.3.0-b4', versionB: '2.17.3.1-b4' })).toBeLessThan(0);
    expect(compareYBSoftwareVersions({ versionA: '2.17.3.0-b4', versionB: '2.17.3.0-b5' })).toBeLessThan(0);
    expect(compareYBSoftwareVersions({ versionA: '2.17.3.0-b20', versionB: '2.17.3.0-b100' })).toBeLessThan(0);
  });
  it('handles software versions without build numbers builds', () => {
    // Custom builds are consider equal if the version core is equal.
    expect(compareYBSoftwareVersions({ versionA: '20.33.55.505', versionB: '20.33.55.505' })).toEqual(0);
    expect(compareYBSoftwareVersions({ versionA: '20.33.55.505-customName', versionB: '20.33.55.505-customName' })).toEqual(
      0
    );
    expect(compareYBSoftwareVersions({ versionA: '20.33.55.505-b404', versionB: '20.33.55.505-customName' })).toEqual(0);
    expect(compareYBSoftwareVersions({ versionA: '20.33.55.505-b9999', versionB: '20.33.55.505-bCustomBuild' })).toEqual(0);

    expect(compareYBSoftwareVersions({ versionA: '3.17.3.0-aCustomBuild', versionB: '2.17.3.0-b4' })).toBeGreaterThan(0);

    expect(compareYBSoftwareVersions({ versionA: '20.33.55.99-b99', versionB: '20.33.55.505-customName' })).toBeLessThan(0);
  });
  it('handles malformed software versions', () => {
    expect(() => compareYBSoftwareVersions({ versionA: '20.33.55', versionB: '20.33.55.505' })).toThrow();
    expect(() =>
      compareYBSoftwareVersions({ versionA: 'anInvalidVersionString', versionB: '20.33.55.505-b404' })
    ).toThrow();
    expect(() => compareYBSoftwareVersions({ versionA: '0000.18.0.0-b2', versionB: '1.33.55.505-custom' })).toThrow();

    expect(compareYBSoftwareVersions({
      versionA: '20.33.55',
      versionB: '20.33.55.505',
      options: {
        suppressFormatError: true
      }
    })).toEqual(0);
    expect(compareYBSoftwareVersions({
      versionA: 'anInvalidVersionString',
      versionB: '20.33.55.505-b404',
      options: {
        suppressFormatError: true
      }
    })).toEqual(
      0
    );
    expect(compareYBSoftwareVersions({
      versionA: '0000.18.0.0-b2',
      versionB: '1.33.55.505-custom',
      options: {
        suppressFormatError: true
      }
    })).toEqual(0);
  });
});
