import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { isBypassEntryAsUrl, isValidBypassProxyEntry } from './bypassProxyEntryValidation';
import { ProxyAdvancedProps } from './dtos';

const isValidHttpsUrl = (s: string | undefined | null): boolean => {
  if (!s?.trim()) return false;
  try {
    return new URL(s.trim()).protocol === 'https:';
  } catch {
    return false;
  }
};

const isValidHttpUrl = (s: string | undefined | null): boolean => {
  if (!s?.trim()) return false;
  try {
    return new URL(s.trim()).protocol === 'http:';
  } catch {
    return false;
  }
};

export const ProxySettingsValidationSchema = (t: TFunction) =>
  Yup.object()
    .shape({
      enableProxyServer: Yup.boolean().required(),
      secureWebProxy: Yup.boolean().required(),
      secureWebProxyServer: Yup.string()
        .test('secure-https', '', function (value) {
          const { enableProxyServer, secureWebProxy } = this.parent as ProxyAdvancedProps;
          if (!enableProxyServer || !secureWebProxy) return true;
          if (!value?.trim()) {
            return this.createError({
              message: t('createUniverseV2.proxySettings.validation.required')
            });
          }
          if (!isValidHttpsUrl(value)) {
            return this.createError({
              message: t('createUniverseV2.proxySettings.validation.httpsUrlInvalid')
            });
          }
          return true;
        })
        .nullable(),
      secureWebProxyPort: Yup.mixed()
        .test('secure-port', '', function (value) {
          const { enableProxyServer, secureWebProxy } = this.parent as ProxyAdvancedProps;
          if (!enableProxyServer || !secureWebProxy) return true;
          if (value === undefined || value === null || value === '') {
            return this.createError({
              message: t('createUniverseV2.proxySettings.validation.required')
            });
          }
          const n = typeof value === 'string' ? parseInt(value, 10) : Number(value);
          if (Number.isNaN(n) || n < 1 || n > 65535) {
            return this.createError({
              message: t('createUniverseV2.proxySettings.validation.required')
            });
          }
          return true;
        })
        .nullable(),
      webProxy: Yup.boolean().required(),
      webProxyServer: Yup.string()
        .test('web-http', '', function (value) {
          const { enableProxyServer, webProxy } = this.parent as ProxyAdvancedProps;
          if (!enableProxyServer || !webProxy) return true;
          if (!value?.trim()) {
            return this.createError({
              message: t('createUniverseV2.proxySettings.validation.required')
            });
          }
          if (!isValidHttpUrl(value)) {
            return this.createError({
              message: t('createUniverseV2.proxySettings.validation.httpUrlInvalid')
            });
          }
          return true;
        })
        .nullable(),
      webProxyPort: Yup.mixed()
        .test('web-port', '', function (value) {
          const { enableProxyServer, webProxy } = this.parent as ProxyAdvancedProps;
          if (!enableProxyServer || !webProxy) return true;
          if (value === undefined || value === null || value === '') {
            return this.createError({
              message: t('createUniverseV2.proxySettings.validation.required')
            });
          }
          const n = typeof value === 'string' ? parseInt(value, 10) : Number(value);
          if (Number.isNaN(n) || n < 1 || n > 65535) {
            return this.createError({
              message: t('createUniverseV2.proxySettings.validation.required')
            });
          }
          return true;
        })
        .nullable(),
      byPassProxyList: Yup.boolean().required(),
      byPassProxyListValues: Yup.array().ensure().of(Yup.string())
    })
    .test('at-least-one-proxy-type', '', function (value) {
      const v = value as ProxyAdvancedProps;
      if (!v?.enableProxyServer) return true;
      if (!v.secureWebProxy && !v.webProxy) {
        return this.createError({
          path: 'proxyServerType',
          message: t('createUniverseV2.proxySettings.validation.selectAtLeastOneProxyType')
        });
      }
      return true;
    })
    .test('bypass-list-validation', '', function (value) {
      const v = value as ProxyAdvancedProps;
      if (!v.enableProxyServer || !v.byPassProxyList) return true;
      const list = v.byPassProxyListValues ?? [];
      const hasNonEmpty = list.some((s) => String(s).trim().length > 0);
      if (!hasNonEmpty) {
        return this.createError({
          path: 'byPassProxyListValues[0]',
          message: t('createUniverseV2.proxySettings.validation.bypassRequired')
        });
      }
      const inner: Yup.ValidationError[] = [];
      for (let i = 0; i < list.length; i++) {
        const s = String(list[i] ?? '').trim();
        if (!s.length) continue;
        if (!isValidBypassProxyEntry(s)) {
          const msg = isBypassEntryAsUrl(s)
            ? t('createUniverseV2.proxySettings.validation.bypassUrlNotAllowed')
            : t('createUniverseV2.proxySettings.validation.bypassInvalidEntry');
          inner.push(
            new Yup.ValidationError(
              msg,
              list[i],
              `byPassProxyListValues[${i}]`,
              'bypass-list-validation'
            )
          );
        }
      }
      if (inner.length) {
        throw new Yup.ValidationError(
          inner as unknown as string[],
          v.byPassProxyListValues,
          'byPassProxyListValues'
        );
      }
      return true;
    });
