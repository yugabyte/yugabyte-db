import { describe, expect, it, vi } from 'vitest';
import { TFunction } from 'i18next';
import { GeneralSettingsValidationSchema } from './ValidationSchema';
import { api } from '../../../../../helpers/api';

vi.mock('../../../../../helpers/api', () => ({
  api: { findUniverseByName: vi.fn().mockResolvedValue([]) }
}));

const t = ((key: string) => key) as TFunction;
const schema = GeneralSettingsValidationSchema(t);
const nameError = async (universeName: string, code: string = 'aws') => {
  try {
    await schema.validateAt('universeName', {
      universeName,
      providerConfigurations: [{ code }]
    } as any);
    return null;
  } catch (e) {
    return (e as Error).message;
  }
};

describe('universeName validation', () => {
  it.each(['a', 'my-univ-1', 'A1', 'Abc-Def'])('accepts %s', async (name) => {
    expect(await nameError(name)).toBeNull();
  });

  it.each(['-a', 'a-', 'a_b', 'a b', 'a.b'])('rejects %s with the format error', async (name) => {
    expect(await nameError(name)).toBe('errMsg.universeNameInvalid');
  });

  it('does not look up the name when the format is invalid', async () => {
    vi.mocked(api.findUniverseByName).mockClear();
    await nameError('a_b');
    expect(api.findUniverseByName).not.toHaveBeenCalled();
    await nameError('ab');
    expect(api.findUniverseByName).toHaveBeenCalledWith('ab');
  });

  it('requires a name', async () => {
    expect(await nameError('')).toBe('errMsg.universeNameRequired');
  });

  it.each(['gcp', 'kubernetes'])('rejects capitals for %s', async (code) => {
    expect(await nameError('MyUniv', code)).toBe('errMsg.universeNameLowercase');
    expect(await nameError('my-univ', code)).toBeNull();
  });
});
