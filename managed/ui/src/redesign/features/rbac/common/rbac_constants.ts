import { ActionType, Resource } from '../permission/IPermission';

export const RbacResourceTypes = {
  UNIVERSE: Resource.UNIVERSE,
  PROVIDER: Resource.DEFAULT,
  STORAGE_CONFIG: Resource.DEFAULT,
  BACKUP: Resource.DEFAULT,
  TASK: Resource.DEFAULT,
  ALERT: Resource.DEFAULT,
  ROLE: Resource.ROLE,
  USER: Resource.USER,
  REPLICATION: Resource.DEFAULT,
  EAR: Resource.DEFAULT,
  EAT: Resource.DEFAULT,
  SUPER_ADMIN: Resource.DEFAULT,
  SUPPORT_BUNDLE: Resource.DEFAULT,
  CERTIFICATE: Resource.DEFAULT,
  CA_CERT: Resource.DEFAULT,
  RUN_TIME_CONFIG: Resource.DEFAULT,
  LDAP: Resource.DEFAULT,
  RELEASES: Resource.DEFAULT
} as const;

export type UserPermission = {
  resourceType: typeof RbacResourceTypes[keyof typeof RbacResourceTypes];
  resourceUUID?: string;
  actions: ActionType[];
};

export const UsersTab = `/admin/rbac?tab=users`;
