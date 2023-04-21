import { compareYBSoftwareVersions } from './universeUtilsTyped';

describe('YB Software version comparison function - compareYBSoftwareVersions', () => {
  it('handles software versions with build numbers', () => {
    expect(compareYBSoftwareVersions('0.0.0.0-b1', '0.0.0.0-b1')).toEqual(0);
    expect(compareYBSoftwareVersions('2.17.3.0-b4', '2.17.3.0-b4')).toEqual(0);
    expect(compareYBSoftwareVersions('1.303.505.44-b405', '1.303.505.44-b405')).toEqual(0);

    expect(compareYBSoftwareVersions('3.17.3.0-b4', '2.17.3.0-b4')).toBeGreaterThan(0);
    expect(compareYBSoftwareVersions('2.18.3.0-b4', '2.17.3.0-b4')).toBeGreaterThan(0);
    expect(compareYBSoftwareVersions('2.17.4.0-b4', '2.17.3.0-b4')).toBeGreaterThan(0);
    expect(compareYBSoftwareVersions('2.17.3.1-b4', '2.17.3.0-b4')).toBeGreaterThan(0);
    expect(compareYBSoftwareVersions('2.17.3.0-b5', '2.17.3.0-b4')).toBeGreaterThan(0);
    expect(compareYBSoftwareVersions('2.17.3.0-b205', '2.17.3.0-b4')).toBeGreaterThan(0);

    expect(compareYBSoftwareVersions('2.17.3.0-b4', '3.17.3.0-b4')).toBeLessThan(0);
    expect(compareYBSoftwareVersions('2.17.3.0-b4', '2.18.3.0-b4')).toBeLessThan(0);
    expect(compareYBSoftwareVersions('2.17.3.0-b4', '2.17.4.0-b4')).toBeLessThan(0);
    expect(compareYBSoftwareVersions('2.17.3.0-b4', '2.17.3.1-b4')).toBeLessThan(0);
    expect(compareYBSoftwareVersions('2.17.3.0-b4', '2.17.3.0-b5')).toBeLessThan(0);
    expect(compareYBSoftwareVersions('2.17.3.0-b20', '2.17.3.0-b100')).toBeLessThan(0);
  });
  it('handles software versions without build numbers builds', () => {
    // Custom builds are consider equal if the version core is equal.
    expect(compareYBSoftwareVersions('20.33.55.505', '20.33.55.505')).toEqual(0);
    expect(compareYBSoftwareVersions('20.33.55.505-customName', '20.33.55.505-customName')).toEqual(
      0
    );
    expect(compareYBSoftwareVersions('20.33.55.505-b404', '20.33.55.505-customName')).toEqual(0);
    expect(compareYBSoftwareVersions('20.33.55.505-b9999', '20.33.55.505-bCustomBuild')).toEqual(0);

    expect(compareYBSoftwareVersions('3.17.3.0-aCustomBuild', '2.17.3.0-b4')).toBeGreaterThan(0);

    expect(compareYBSoftwareVersions('20.33.55.99-b99', '20.33.55.505-customName')).toBeLessThan(0);
  });
  it('handles malformed software versions', () => {
    expect(() => compareYBSoftwareVersions('20.33.55', '20.33.55.505')).toThrow();
    expect(() =>
      compareYBSoftwareVersions('anInvalidVersionString', '20.33.55.505-b404')
    ).toThrow();
    expect(() => compareYBSoftwareVersions('0000.18.0.0-b2', '1.33.55.505-custom')).toThrow();

    expect(compareYBSoftwareVersions('20.33.55', '20.33.55.505', true)).toEqual(0);
    expect(compareYBSoftwareVersions('anInvalidVersionString', '20.33.55.505-b404', true)).toEqual(
      0
    );
    expect(compareYBSoftwareVersions('0000.18.0.0-b2', '1.33.55.505-custom', true)).toEqual(0);
  });
});
