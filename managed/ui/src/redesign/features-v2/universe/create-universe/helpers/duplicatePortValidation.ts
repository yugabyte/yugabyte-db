import * as Yup from 'yup';

export const CONNECTION_POOLING_PORT_FIELD_NAMES = [
  'ysqlServerRpcPort',
  'internalYsqlServerRpcPort'
] as const;

export const COMMUNICATION_PORT_FIELD_NAMES = [
  'masterHttpPort',
  'masterRpcPort',
  'tserverHttpPort',
  'tserverRpcPort',
  'ysqlServerHttpPort',
  'ysqlServerRpcPort',
  'internalYsqlServerRpcPort',
  'yqlServerHttpPort',
  'yqlServerRpcPort',
  'redisServerHttpPort',
  'redisServerRpcPort',
  'nodeExporterPort',
  'ybControllerRpcPort'
] as const;

export type CommunicationPortFieldName = (typeof COMMUNICATION_PORT_FIELD_NAMES)[number];

const parsePort = (raw: unknown): number | undefined => {
  if (raw === undefined || raw === null || raw === '') return undefined;
  const port = typeof raw === 'number' ? raw : Number(String(raw).replace(/\D/g, ''));
  return Number.isFinite(port) ? port : undefined;
};

/** Field names whose current values collide with at least one other listed field. */
export function findDuplicatePortFieldNames(
  values: Record<string, unknown>,
  fieldNames: readonly string[]
): string[] {
  const portToFields = new Map<number, string[]>();

  for (const name of fieldNames) {
    const port = parsePort(values[name]);
    if (port === undefined) continue;
    const existing = portToFields.get(port);
    if (existing) {
      existing.push(name);
    } else {
      portToFields.set(port, [name]);
    }
  }

  return Array.from(portToFields.values())
    .filter((fields) => fields.length > 1)
    .reduce<string[]>((acc, fields) => acc.concat(fields), []);
}

export function throwDuplicatePortsYupError(
  duplicateFields: string[],
  message: string,
  createError: (params: { path: string; message: string }) => Yup.ValidationError,
  path: string
): true {
  if (duplicateFields.length === 0) return true;

  const fieldErrors = duplicateFields.map((fieldPath) => createError({ path: fieldPath, message }));
  const error = new Yup.ValidationError(
    fieldErrors.map((e) => e.message),
    'errors',
    path
  );
  error.inner = fieldErrors;
  throw error;
}
