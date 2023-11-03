import { RbacValidatorProps } from "./common/RbacValidator";
import { RbacResourceTypes } from "./common/rbac_constants";
import { Action, Resource } from "./permission";

type UserPermissionMapType = Record<string, Pick<RbacValidatorProps["accessRequiredOn"], "resourceType" | "permissionRequired"> | Pick<RbacValidatorProps["accessRequiredOn"], "onResource">>
export const UserPermissionMap = {
    SUPER_ADMIN: {
        resourceType: RbacResourceTypes.SUPER_ADMIN,
        permissionRequired: [Action.SUPER_ADMIN_ACTIONS],
        onResource: undefined
    },
    //universe
    createUniverse: {
        resourceType: RbacResourceTypes.UNIVERSE,
        permissionRequired: [Action.CREATE],
    },
    editUniverse: {
        resourceType: RbacResourceTypes.UNIVERSE,
        permissionRequired: [Action.READ, Action.UPDATE]
    },
    readUniverse: {
        resourceType: RbacResourceTypes.UNIVERSE,
        permissionRequired: [Action.READ]
    },
    updateUniverse: {
        resourceType: RbacResourceTypes.UNIVERSE,
        permissionRequired: [Action.UPDATE]
    },
    pauseResumeUniverse: {
        resourceType: RbacResourceTypes.UNIVERSE,
        permissionRequired: [Action.PAUSE_RESUME]
    },
    deleteUniverse: {
        resourceType: RbacResourceTypes.UNIVERSE,
        permissionRequired: [Action.DELETE]
    },

    //Backup
    listBackup: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.BACKUP,
        permissionRequired: [Action.READ]
    },
    createBackup: {
        resourceType: Resource.UNIVERSE,
        permissionRequired: [Action.BACKUP_RESTORE]
    },
    deleteBackup: {
        resourceType: RbacResourceTypes.BACKUP,
        permissionRequired: [Action.DELETE]
    },
    restoreBackup: {
        resourceType: Resource.UNIVERSE,
        permissionRequired: [Action.BACKUP_RESTORE]
    },
    changeRetentionPeriod: {
        resourceType: RbacResourceTypes.BACKUP,
        permissionRequired: [Action.UPDATE]
    },
    editBackup: {
        resourceType: RbacResourceTypes.BACKUP,
        permissionRequired: [Action.UPDATE]
    },
    // Roles
    createRole: {
        resourceType: RbacResourceTypes.ROLE,
        permissionRequired: [Action.CREATE]
    },
    listRole: {
        resourceType: RbacResourceTypes.ROLE,
        permissionRequired: [Action.READ]
    },
    deleteRole: {
        resourceType: RbacResourceTypes.ROLE,
        permissionRequired: [Action.DELETE]
    },
    editRole: {
        resourceType: RbacResourceTypes.ROLE,
        permissionRequired: [Action.UPDATE]
    },

    // Provider
    createProvider: {
        resourceType: RbacResourceTypes.PROVIDER,
        permissionRequired: [Action.CREATE]
    },
    listProvider: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.PROVIDER,
        permissionRequired: [Action.READ]
    },
    deleteProvider: {
        resourceType: RbacResourceTypes.PROVIDER,
        permissionRequired: [Action.DELETE]
    },

    // storage Configuration
    createStorageConfiguration: {
        resourceType: RbacResourceTypes.STORAGE_CONFIG,
        permissionRequired: [Action.CREATE]
    },
    listStorageConfiguration: {
        resourceType: RbacResourceTypes.STORAGE_CONFIG,
        permissionRequired: [Action.READ]
    },
    editStorageConfiguration: {
        resourceType: RbacResourceTypes.STORAGE_CONFIG,
        permissionRequired: [Action.UPDATE]
    },
    deleteStorageConfiguration: {
        resourceType: RbacResourceTypes.STORAGE_CONFIG,
        permissionRequired: [Action.DELETE]
    },

    // Encryption at rest
    createEncryptionAtRest: {
        resourceType: RbacResourceTypes.EAR,
        permissionRequired: [Action.CREATE]
    },
    editEncryptionAtRest: {
        resourceType: RbacResourceTypes.EAR,
        permissionRequired: [Action.UPDATE]
    },
    deleteEncryptionAtRest: {
        resourceType: RbacResourceTypes.EAR,
        permissionRequired: [Action.DELETE]
    },

    // Encryption at transit

    createEncryptionInTransit: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.EAT,
        permissionRequired: [Action.CREATE]
    },
    listEAT: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.EAT,
        permissionRequired: [Action.READ]
    },
    editEncryptionInTransit: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.EAT,
        permissionRequired: [Action.UPDATE]
    },
    deleteEncryptionInTransit: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.EAT,
        permissionRequired: [Action.DELETE]
    },

    //Alerts
    readAlerts: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.ALERT,
        permissionRequired: [Action.READ]
    },
    acknowledgeAlert: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.ALERT,
        permissionRequired: [Action.UPDATE]
    },
    //Tasks
    readTask: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.TASK,
        permissionRequired: [Action.READ]
    },
    abortTask: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.TASK,
        permissionRequired: [Action.UPDATE]
    },
    //Xcluster
    viewReplication: {
        resourceType: RbacResourceTypes.REPLICATION,
        permissionRequired: [Action.READ]
    },

    createReplication: {
        resourceType: RbacResourceTypes.REPLICATION,
        permissionRequired: [Action.CREATE]
    },

    editReplication: {
        resourceType: RbacResourceTypes.REPLICATION,
        permissionRequired: [Action.UPDATE]
    },


    // alert config
    readAlertsConfig: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.ALERT,
        permissionRequired: [Action.READ]
    },
    createAlertsConfig: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.ALERT,
        permissionRequired: [Action.CREATE]
    },
    editAlertsConfig: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.ALERT,
        permissionRequired: [Action.UPDATE]
    },
    deleteAlertsConfig: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.ALERT,
        permissionRequired: [Action.DELETE]
    },
    sendTestAlert: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.ALERT,
        permissionRequired: [Action.UPDATE]
    },
    createCustomVariable: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.ALERT,
        permissionRequired: [Action.CREATE]
    },
    createMaintenenceWindow: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.ALERT,
        permissionRequired: [Action.CREATE]
    },
    editMaintenanceWindow: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.ALERT,
        permissionRequired: [Action.UPDATE]
    },
    deleteMaintenanceWindow: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.ALERT,
        permissionRequired: [Action.DELETE]
    },
    // Performance Advisor
    rescan: {
        resourceType: RbacResourceTypes.UNIVERSE,
        permissionRequired: [Action.UPDATE]
    },

    //support bundle
    listSupportBundle: {
        onResource: "CUSTOMER_ID",
        resourceType: RbacResourceTypes.SUPPORT_BUNDLE,
        permissionRequired: [Action.READ]
    },
    createSupportBundle: {
        onResource: "CUSTOMER_ID",
        resourceType: RbacResourceTypes.SUPPORT_BUNDLE,
        permissionRequired: [Action.CREATE]
    },
    deleteSupportBundle: {
        onResource: "CUSTOMER_ID",
        resourceType: RbacResourceTypes.SUPPORT_BUNDLE,
        permissionRequired: [Action.DELETE]
    },
    downloadSupportBundle: {
        onResource: "CUSTOMER_ID",
        resourceType: RbacResourceTypes.SUPPORT_BUNDLE,
        permissionRequired: [Action.CREATE]
    },

    //CA-Certs

    listCACerts: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.CA_CERT,
        permissionRequired: [Action.READ]
    },
    createCACerts: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.CA_CERT,
        permissionRequired: [Action.CREATE]
    },
    deleteCACerts: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.CA_CERT,
        permissionRequired: [Action.DELETE]
    },

    editRuntimeConfig: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.RUN_TIME_CONFIG,
        permissionRequired: [Action.UPDATE]
    },
    listRuntimeConfig: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.RUN_TIME_CONFIG,
        permissionRequired: [Action.READ]
    },

    //Create user

    createUser: {
        onResource: undefined,
        resourceType: RbacResourceTypes.USER,
        permissionRequired: [Action.CREATE]
    },
    listUser: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.USER,
        permissionRequired: [Action.READ]
    },
    updateUser: {
        resourceType: RbacResourceTypes.USER,
        permissionRequired: [Action.UPDATE_ROLE_BINDINGS]
    },
    deleteUser: {
        onResource: 'CUSTOMER_ID',
        resourceType: RbacResourceTypes.USER,
        permissionRequired: [Action.DELETE]
    },


    //LDAP

    updateLDAP: {
        resourceType: RbacResourceTypes.LDAP,
        permissionRequired: [Action.UPDATE]
    },
    importRelease: {
        resourceType: RbacResourceTypes.RELEASES,
        permissionRequired: [Action.CREATE]
    },
    disableRelease: {
        resourceType: RbacResourceTypes.RELEASES,
        permissionRequired: [Action.UPDATE]
    },
    deleteRelease: {
        resourceType: RbacResourceTypes.RELEASES,
        permissionRequired: [Action.DELETE]
    }
} satisfies UserPermissionMapType;
