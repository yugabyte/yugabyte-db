import { ActionType, ResourceType } from "./permission";

export const ApiRequestType = {
    GET: 'GET',
    POST: 'POST',
    DELETE: 'DELETE',
    PUT: 'PUT'
} as const;

export const Operators = {
    AND: 'AND',
    OR: 'OR',
    NOT: 'NOT'
};

export type ApiEndpointPermMappingType = {
    requestType: keyof typeof ApiRequestType;
    endpoint: string;
    rbacPermissionDefinitions: {
        operator: keyof typeof Operators;
        rbacPermissionDefinitionList: {
            operator: keyof typeof Operators;
            rbacPermissionList: {
                resourceType: ResourceType;
                action: ActionType;
            }[]
        }[]
    }
}[]

export type ApiProps = {
    requestType: ApiEndpointPermMappingType[number]['requestType'];
    endpoint: ApiEndpointPermMappingType[number]['endpoint']
};

type ApiPermissionMapType = Record<string, ApiProps>;


export const ApiPermissionMap = {

    GET_CUSTOMERS: { requestType: ApiRequestType.GET, endpoint: '/customers' },
    MODIFY_CUSTOMER: { requestType: ApiRequestType.PUT, endpoint: '/customers/$cUUID<[^/]+>' },
    DELETE_CUSTOMER: { requestType: ApiRequestType.DELETE, endpoint: '/customers/$cUUID<[^/]+>' },
    GET_CUSTOMER: { requestType: ApiRequestType.GET, endpoint: '/customers/$cUUID<[^/]+>' },
    GET_ACCESS_KEYS: { requestType: ApiRequestType.GET, endpoint: '/access_keys' },
    GET_ADMIN_NOTIFICATIONS: {
        requestType: ApiRequestType.GET,
        endpoint: '/admin_notifications'
    },
    GET_ALERT_CHANNEL_TEMPLATES: {
        requestType: ApiRequestType.GET,
        endpoint: '/alert_channel_templates'
    },
    DELETE_ALERT_CHANNEL_TEMPLATE: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/alert_channel_templates/$acType<[^/]+>'
    },
    GET_ALERT_CHANNEL_TEMPLATE: {
        requestType: ApiRequestType.GET,
        endpoint: '/alert_channel_templates/$acType<[^/]+>'
    },
    CREATE_ALERT_CHANNEL_TEMPLATE: {
        requestType: ApiRequestType.POST,
        endpoint: '/alert_channel_templates/$acType<[^/]+>'
    },
    CREATE_ALERT_CHANNEL: { requestType: ApiRequestType.POST, endpoint: '/alert_channels' },
    GET_ALERT_CHANNELS: { requestType: ApiRequestType.GET, endpoint: '/alert_channels' },
    GET_ALERT_CHANNEL: {
        requestType: ApiRequestType.GET,
        endpoint: '/alert_channels/$acUUID<[^/]+>'
    },
    MODIFY_ALERT_CHANNEL: {
        requestType: ApiRequestType.PUT,
        endpoint: '/alert_channels/$acUUID<[^/]+>'
    },
    DELETE_ALERT_CHANNEL: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/alert_channels/$acUUID<[^/]+>'
    },
    CREATE_ALERT_CONFIGURATIONS: {
        requestType: ApiRequestType.POST,
        endpoint: '/alert_configurations'
    },
    DELETE_ALERT_CONFIGURATIONS: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/alert_configurations/$configurationUUID<[^/]+>'
    },
    GET_ALERT_CONFIGURATION: {
        requestType: ApiRequestType.GET,
        endpoint: '/alert_configurations/$configurationUUID<[^/]+>'
    },
    MODIFY_ALERT_CONFIGURATIONS: {
        requestType: ApiRequestType.PUT,
        endpoint: '/alert_configurations/$configurationUUID<[^/]+>'
    },
    SEND_TEST_ALERT: {
        requestType: ApiRequestType.POST,
        endpoint: '/alert_configurations/$configurationUUID<[^/]+>/test_alert'
    },
    GET_ALERT_CONFIGURATIONS: {
        requestType: ApiRequestType.POST,
        endpoint: '/alert_configurations/list'
    },
    GET_ALERT_CONFIGURATIONS_BY_PAGE: {
        requestType: ApiRequestType.POST,
        endpoint: '/alert_configurations/page'
    },
    GET_ALERT_DESTINATIONS: {
        requestType: ApiRequestType.GET,
        endpoint: '/alert_destinations'
    },
    CREATE_ALERT_DESTINATION: {
        requestType: ApiRequestType.POST,
        endpoint: '/alert_destinations'
    },
    GET_ALERT_DESTINATION: {
        requestType: ApiRequestType.GET,
        endpoint: '/alert_destinations/$adUUID<[^/]+>'
    },
    MODIFY_ALERT_DESTINATION: {
        requestType: ApiRequestType.PUT,
        endpoint: '/alert_destinations/$adUUID<[^/]+>'
    },
    DELETE_ALERT_DESTINATION: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/alert_destinations/$adUUID<[^/]+>'
    },
    PREVIEW_ALERT_NOTIFICATION: {
        requestType: ApiRequestType.POST,
        endpoint: '/alert_notification_preview'
    },
    MODIFY_ALERT_TEMPLATE_SETTING: {
        requestType: ApiRequestType.PUT,
        endpoint: '/alert_template_settings'
    },
    GET_ALERT_TEMPLATE_SETTINGS: {
        requestType: ApiRequestType.GET,
        endpoint: '/alert_template_settings'
    },
    DELETE_ALERT_TEMPLATE_SETTING: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/alert_template_settings/$settingsUUID<[^/]+>'
    },
    MODIFY_ALERT_TEMPLATE_VARIABLE: {
        requestType: ApiRequestType.PUT,
        endpoint: '/alert_template_variables'
    },
    GET_ALERT_TEMPLATE_VARIABLES: {
        requestType: ApiRequestType.GET,
        endpoint: '/alert_template_variables'
    },
    DELETE_ALERT_TEMPLATE_VARIABLE: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/alert_template_variables/$variableUUID<[^/]+>'
    },
    CREATE_ALERT_TEMPLATE: { requestType: ApiRequestType.POST, endpoint: '/alert_templates' },
    GET_ALERTS: { requestType: ApiRequestType.GET, endpoint: '/alerts' },
    GET_ALERT: {
        requestType: ApiRequestType.GET,
        endpoint: '/alerts/$alertUUID<[^/]+>'
    },
    ACKNOWLEDGE_ALERT: {
        requestType: ApiRequestType.POST,
        endpoint: '/alerts/acknowledge'
    },
    GET_ACTIVE_ALERTS: { requestType: ApiRequestType.GET, endpoint: '/alerts/active' },
    GET_ALERTS_COUNT: { requestType: ApiRequestType.POST, endpoint: '/alerts/count' },
    GET_ALERTS_BY_PAGE: { requestType: ApiRequestType.POST, endpoint: '/alerts/page' },

    GET_API_TOKEN: { requestType: ApiRequestType.PUT, endpoint: '/api_token' },

    CREATE_BACKUP: { requestType: ApiRequestType.POST, endpoint: '/backups' },

    EDIT_BACKUP: {
        requestType: ApiRequestType.PUT,
        endpoint: '/backups/$backupUUID<[^/]+>'
    },
    GET_BACKUP: {
        requestType: ApiRequestType.GET,
        endpoint: '/backups/$backupUUID<[^/]+>'
    },
    GET_INCREMENTAL_BACKUPS: {
        requestType: ApiRequestType.GET,
        endpoint: '/backups/$backupUUID<[^/]+>/list_increments'
    },
    ABORT_BACKUP: {
        requestType: ApiRequestType.POST,
        endpoint: '/backups/$backupUUID<[^/]+>/stop'
    },
    DELETE_BACKUP: { requestType: ApiRequestType.POST, endpoint: '/backups/delete' },

    GET_BACKUPS_BY_PAGE: { requestType: ApiRequestType.POST, endpoint: '/backups/page' },
    CREATE_CERTIFICATE: { requestType: ApiRequestType.POST, endpoint: '/certificates' },
    GET_CERTIFICATES: { requestType: ApiRequestType.GET, endpoint: '/certificates' },
    DELETE_CERTIFICATE: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/certificates/$rUUID<[^/]+>'
    },

    DOWNLOAD_CERTIFICATE: {
        requestType: ApiRequestType.GET,
        endpoint: '/certificates/$rUUID<[^/]+>/download'
    },
    MODIFY_CERTIFICATE: {
        requestType: ApiRequestType.POST,
        endpoint: '/certificates/$rUUID<[^/]+>/edit'
    },

    GET_BUCKETS_LIST: {
        requestType: ApiRequestType.POST,
        endpoint: '/cloud/$cloud<[^/]+>/buckets'
    },
    CREATE_CUSTOMER_CONFIG: { requestType: ApiRequestType.POST, endpoint: '/configs' },
    GET_CUSTOMER_CONFIGS: { requestType: ApiRequestType.GET, endpoint: '/configs' },
    EDIT_CUSTOMER_CONFIG: {
        requestType: ApiRequestType.PUT,
        endpoint: '/configs/$configUUID<[^/]+>'
    },
    DELETE_CUSTOMER_CONFIG: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/configs/$configUUID<[^/]+>'
    },
    CREATE_BACKUP_SCHEDULE: {
        requestType: ApiRequestType.POST,
        endpoint: '/create_backup_schedule_async'
    },
    CREATE_CA_CERT: { requestType: ApiRequestType.POST, endpoint: '/customCAStore' },
    MODIFY_CA_CERT: {
        requestType: ApiRequestType.POST,
        endpoint: '/customCAStore/$certUUID<[^/]+>'
    },
    GET_CA_CERTS: {
        requestType: ApiRequestType.GET,
        endpoint: '/customCAStoreCertificates'
    },
    DELETE_CA_CERTS: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/customCAStoreCertificates/$certUUID<[^/]+>'
    },
    GET_CA_CERT: {
        requestType: ApiRequestType.GET,
        endpoint: '/customCAStoreCertificates/$certUUID<[^/]+>'
    },
    GET_FEATURES: { requestType: ApiRequestType.PUT, endpoint: '/features' },

    GET_HOST_INFO: { requestType: ApiRequestType.GET, endpoint: '/host_info' },
    GET_KMS_CONFIGS: { requestType: ApiRequestType.GET, endpoint: '/kms_configs' },
    DELETE_KMS_PROVIDER_CONFIG: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/kms_configs/$configUUID<[^/]+>'
    },
    GET_KMS_CONFIG: {
        requestType: ApiRequestType.GET,
        endpoint: '/kms_configs/$configUUID<[^/]+>'
    },
    MODIFY_KMS_CONFIG: {
        requestType: ApiRequestType.POST,
        endpoint: '/kms_configs/$configUUID<[^/]+>/edit'
    },

    UPDATE_KMS_CONFIG: {
        requestType: ApiRequestType.POST,
        endpoint: '/kms_configs/$kmsProvider<[^/]+>'
    },
    GET_LDAP_MAPPINGS: { requestType: ApiRequestType.GET, endpoint: '/ldap_mappings' },
    UPDATE_LDAP_MAPPING: { requestType: ApiRequestType.PUT, endpoint: '/ldap_mappings' },

    CREATE_MAINTENANCE_WINDOW: {
        requestType: ApiRequestType.POST,
        endpoint: '/maintenance_windows'
    },
    MODIFY_MAINTENANCE_WINDOW: {
        requestType: ApiRequestType.PUT,
        endpoint: '/maintenance_windows/$windowUUID<[^/]+>'
    },
    DELETE_MAINTENANCE_WINDOW: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/maintenance_windows/$windowUUID<[^/]+>'
    },
    GET_MAINTENANCE_WINDOW: {
        requestType: ApiRequestType.GET,
        endpoint: '/maintenance_windows/$windowUUID<[^/]+>'
    },
    GET_MAINTENANCE_WINDOWS: {
        requestType: ApiRequestType.POST,
        endpoint: '/maintenance_windows/list'
    },
    GET_MAINTENANCE_WINDOWS_BY_PAGE: {
        requestType: ApiRequestType.POST,
        endpoint: '/maintenance_windows/page'
    },
    GET_METRICS: { requestType: ApiRequestType.POST, endpoint: '/metrics' },
    GET_NODE_AGENTS: { requestType: ApiRequestType.GET, endpoint: '/node_agents' },
    GET_NODE_AGENT_BY_ID: {
        requestType: ApiRequestType.GET,
        endpoint: '/node_agents/$nUUID<[^/]+>'
    },
    GET_NODE_AGENT_BY_PAGE: { requestType: ApiRequestType.POST, endpoint: '/node_agents/page' },
    GET_NODE_AGENTS_BY_LIST: {
        requestType: ApiRequestType.GET,
        endpoint: '/nodes/$nodeUUID<[^/]+>/list'
    },
    GET_PROVIDERS: { requestType: ApiRequestType.GET, endpoint: '/providers' },
    CREATE_PROVIDERS: { requestType: ApiRequestType.POST, endpoint: '/providers' },
    GET_PROVIDER_BY_ID: {
        requestType: ApiRequestType.GET,
        endpoint: '/providers/$pUUID<[^/]+>'
    },
    DELETE_PROVIDER: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/providers/$pUUID<[^/]+>'
    },
    GET_PROVIDER_ACCESS_KEYS: {
        requestType: ApiRequestType.GET,
        endpoint: '/providers/$pUUID<[^/]+>/access_keys'
    },
    CREATE_PROVIDER_ACCESS_KEYS: {
        requestType: ApiRequestType.POST,
        endpoint: '/providers/$pUUID<[^/]+>/access_keys'
    },
    GET_ACCESS_KEY_BY_KEY_CODE: {
        requestType: ApiRequestType.GET,
        endpoint: '/providers/$pUUID<[^/]+>/access_keys/$keyCode<[^/]+>'
    },
    UPDATE_ACCESS_KEY_BY_KEY_CODE: {
        requestType: ApiRequestType.PUT,
        endpoint: '/providers/$pUUID<[^/]+>/access_keys/$keyCode<[^/]+>'
    },
    DELETE_ACCESS_KEY_BY_KEY_CODE: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/providers/$pUUID<[^/]+>/access_keys/$keyCode<[^/]+>'
    },
    GET_BOOTSTRAP_PROVIDER: {
        requestType: ApiRequestType.POST,
        endpoint: '/providers/$pUUID<[^/]+>/bootstrap'
    },
    MODIFY_PROVIDER: {
        requestType: ApiRequestType.PUT,
        endpoint: '/providers/$pUUID<[^/]+>/edit'
    },
    INITIALIZE_PROVIDER: {
        requestType: ApiRequestType.GET,
        endpoint: '/providers/$pUUID<[^/]+>/initialize'
    },
    GET_PROVIDER_ENDPOINT: {
        requestType: ApiRequestType.POST,
        endpoint: '/providers/$pUUID<[^/]+>/instance_types'
    },
    GET_INSTANCE_TYPES: {
        requestType: ApiRequestType.GET,
        endpoint: '/providers/$pUUID<[^/]+>/instance_types'
    },
    DELETE_INSTANCE_TYPES: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/providers/$pUUID<[^/]+>/instance_types/$code<[^/]+>'
    },
    GET_INSTANCE_TYPE_BY_CODE: {
        requestType: ApiRequestType.GET,
        endpoint: '/providers/$pUUID<[^/]+>/instance_types/$code<[^/]+>'
    },
    DELETE_INSTANCE: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/providers/$pUUID<[^/]+>/instances/$instanceIP<[^/]+>'
    },
    PRECHECK_INSTANCE: {
        requestType: ApiRequestType.POST,
        endpoint: '/providers/$pUUID<[^/]+>/instances/$instanceIP<[^/]+>'
    },
    GET_NODES_LIST: {
        requestType: ApiRequestType.GET,
        endpoint: '/providers/$pUUID<[^/]+>/nodes/list'
    },
    CREATE_REGION_ZONES: {
        requestType: ApiRequestType.POST,
        endpoint: '/providers/$pUUID<[^/]+>/provider_regions/$rUUID<[^/]+>/region_zones'
    },
    MODIFY_REGION_ZONES: {
        requestType: ApiRequestType.PUT,
        endpoint: '/providers/$pUUID<[^/]+>/provider_regions/$rUUID<[^/]+>/region_zones/$azUUID<[^/]+>'
    },

    CREATE_REGION_BY_PROVIDER: {
        requestType: ApiRequestType.GET,
        endpoint: '/providers/$pUUID<[^/]+>/regions'
    },
    GET_REGIONS_BY_PROVIDER: {
        requestType: ApiRequestType.POST,
        endpoint: '/providers/$pUUID<[^/]+>/regions'
    },
    DELETE_REGION_BY_PROVIDER: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/providers/$pUUID<[^/]+>/regions/$rUUID<[^/]+>'
    },
    MODIFY_REGION_BY_PROVIDER: {
        requestType: ApiRequestType.PUT,
        endpoint: '/providers/$pUUID<[^/]+>/regions/$rUUID<[^/]+>'
    },
    CREATE_ZONE: {
        requestType: ApiRequestType.POST,
        endpoint: '/providers/$pUUID<[^/]+>/regions/$rUUID<[^/]+>/zones'
    },
    GET_ZONES: {
        requestType: ApiRequestType.GET,
        endpoint: '/providers/$pUUID<[^/]+>/regions/$rUUID<[^/]+>/zones'
    },
    MODIFY_ZONE: {
        requestType: ApiRequestType.PUT,
        endpoint: '/providers/$pUUID<[^/]+>/regions/$rUUID<[^/]+>/zones/$azUUID<[^/]+>'
    },
    DELETE_ZONE: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/providers/$pUUID<[^/]+>/regions/$rUUID<[^/]+>/zones/$azUUID<[^/]+>'
    },
    GET_RELEASES_BY_PROVIDER: {
        requestType: ApiRequestType.GET,
        endpoint: '/providers/$pUUID<[^/]+>/releases'
    },
    CREATE_KUBERNETES_PROVIDER: {
        requestType: ApiRequestType.POST,
        endpoint: '/providers/kubernetes'
    },
    GET_REGION_METADATA: {
        requestType: ApiRequestType.GET,
        endpoint: '/providers/region_metadata/$code<[^/]+>'
    },
    CREATE_DOCKER_PROVIDER: {
        requestType: ApiRequestType.POST,
        endpoint: '/providers/setup_docker'
    },
    GET_KUBERNETES_CONFIG: {
        requestType: ApiRequestType.GET,
        endpoint: '/providers/suggested_kubernetes_config'
    },
    CREATE_ON_PREM_PROVIDER: { requestType: ApiRequestType.POST, endpoint: '/providers/ui' },

    GET_RBAC_PERMISSIONS: { requestType: ApiRequestType.GET, endpoint: '/rbac/permissions' },
    CREATE_RBAC_ROLE: { requestType: ApiRequestType.POST, endpoint: '/rbac/role' },
    GET_RBAC_ROLES: { requestType: ApiRequestType.GET, endpoint: '/rbac/role' },
    GET_RBAC_ROLE_BINDING: { requestType: ApiRequestType.GET, endpoint: '/rbac/role_binding' },
    CREATE_USER_ROLE_BINDINGS: {
        requestType: ApiRequestType.POST,
        endpoint: '/rbac/role_binding/$userUUID<[^/]+>'
    },
    GET_RBAC_ROLE: {
        requestType: ApiRequestType.GET,
        endpoint: '/rbac/role/$rUUID<[^/]+>'
    },
    DELETE_RBAC_ROLE: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/rbac/role/$rUUID<[^/]+>'
    },
    MODIFY_RBAC_ROLE: {
        requestType: ApiRequestType.PUT,
        endpoint: '/rbac/role/$rUUID<[^/]+>'
    },
    GET_RBAC_USER: {
        requestType: ApiRequestType.GET,
        endpoint: '/rbac/user/$userUUID<[^/]+>'
    },
    GET_REGIONS: { requestType: ApiRequestType.GET, endpoint: '/regions' },
    GET_RELEASES: { requestType: ApiRequestType.GET, endpoint: '/releases' },
    MODIFY_RELEASE: { requestType: ApiRequestType.PUT, endpoint: '/releases' },
    CREATE_RELEASE: { requestType: ApiRequestType.POST, endpoint: '/releases' },
    MODIFY_RELEASE_BY_NAME: {
        requestType: ApiRequestType.PUT,
        endpoint: '/releases/$name<[^/]+>'
    },
    DELETE_RELEASE_BY_NAME: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/releases/$name<[^/]+>'
    },
    RESTORE_BACKUP: { requestType: ApiRequestType.POST, endpoint: '/restore' },
    GET_RESTORES_BY_PAGE: { requestType: ApiRequestType.POST, endpoint: '/restore/page' },
    GET_RESTORE_PREFLIGHT_CHECK: {
        requestType: ApiRequestType.POST,
        endpoint: '/restore/preflight'
    },
    GET_RUNTIME_CONFIG_BY_SCOPE: {
        requestType: ApiRequestType.GET,
        endpoint: '/runtime_config/$scope<[^/]+>'
    },
    MODIFY_RUNTIME_CONFIG_BY_KEY: {
        requestType: ApiRequestType.PUT,
        endpoint: '/runtime_config/$scope<[^/]+>/key/$key<[^/]+>'
    },
    DELETE_RUNTIME_CONFIG_BY_KEY: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/runtime_config/$scope<[^/]+>/key/$key<[^/]+>'
    },
    GET_RUNTIME_CONFIG_BY_KEY: {
        requestType: ApiRequestType.GET,
        endpoint: '/runtime_config/$scope<[^/]+>/key/$key<[^/]+>'
    },
    GET_RUNTIME_CONFIG_SCOPES: {
        requestType: ApiRequestType.GET,
        endpoint: '/runtime_config/scopes'
    },
    GET_SCHEDULES: { requestType: ApiRequestType.GET, endpoint: '/schedules' },
    MODIFY_SCHEDULE: {
        requestType: ApiRequestType.PUT,
        endpoint: '/schedules/$sUUID<[^/]+>'
    },
    DELETE_SCHEDULE: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/schedules/$sUUID<[^/]+>'
    },
    GET_SCHEDULE: {
        requestType: ApiRequestType.GET,
        endpoint: '/schedules/$sUUID<[^/]+>'
    },
    DELETE_SCHEDULE_BY_ID: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/schedules/$sUUID<[^/]+>/delete'
    },
    GET_SCHEDULES_BY_PAGE: { requestType: ApiRequestType.POST, endpoint: '/schedules/page' },
    GET_TASKS: { requestType: ApiRequestType.GET, endpoint: '/tasks' },
    GET_TASKS_LIST: { requestType: ApiRequestType.GET, endpoint: '/tasks_list' },
    GET_TASK_BY_ID: {
        requestType: ApiRequestType.GET,
        endpoint: '/tasks/$tUUID<[^/]+>'
    },
    ABORT_TASK: {
        requestType: ApiRequestType.POST,
        endpoint: '/tasks/$tUUID<[^/]+>/abort'
    },

    GET_FAILED_TASKS: {
        requestType: ApiRequestType.GET,
        endpoint: '/tasks/$tUUID<[^/]+>/failed'
    },
    GET_FAILED_SUB_TASKS: {
        requestType: ApiRequestType.GET,
        endpoint: '/tasks/$tUUID<[^/]+>/failed_subtasks'
    },
    RETRY_TASKS: {
        requestType: ApiRequestType.POST,
        endpoint: '/tasks/$tUUID<[^/]+>/retry'
    },
    CONFIGURE_UNIVERSE: {
        requestType: ApiRequestType.POST,
        endpoint: '/universe_configure'
    },
    CONFIGURE_RESOURCE: {
        requestType: ApiRequestType.POST,
        endpoint: '/universe_resources'
    },
    CONFIGURE_UNIVERSE_UPDATE_OPTIONS: {
        requestType: ApiRequestType.POST,
        endpoint: '/universe_update_options'
    },
    CREATE_UNIVERSE: { requestType: ApiRequestType.POST, endpoint: '/universes' },
    GET_UNIVERSES: { requestType: ApiRequestType.GET, endpoint: '/universes' },
    GET_UNIVERSES_BY_ID: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>'
    },
    MODIFY_UNIVERSE: {
        requestType: ApiRequestType.PUT,
        endpoint: '/universes/$uniUUID<[^/]+>'
    },
    DELETE_UNIVERSE: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/universes/$uniUUID<[^/]+>'
    },
    DOWNLOAD_UNIVERSE_NODE_LOGS: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/$nodeName<[^/]+>/download_logs'
    },
    GET_UNIVERSE_BACKUPS: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/backups'
    },
    UNIVERSE_RESTORE_BACKUP: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/backups/restore'
    },

    CREATE_READ_REPLICA: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/cluster'
    },
    DELETE_READ_REPLICA: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/universes/$uniUUID<[^/]+>/cluster/$clustUUID<[^/]+>'
    },
    MODIFY_PRIMARY_CLUSTER: {
        requestType: ApiRequestType.PUT,
        endpoint: '/universes/$uniUUID<[^/]+>/clusters/primary'
    },
    CREATE_READ_ONLY_CLUSTER: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/clusters/read_only'
    },
    MODIFY_READ_ONLY_CLUSTER: {
        requestType: ApiRequestType.PUT,
        endpoint: '/universes/$uniUUID<[^/]+>/clusters/read_only'
    },
    DELETE_READ_ONLY_CLUSTER: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/universes/$uniUUID<[^/]+>/clusters/read_only/$clustUUID<[^/]+>'
    },
    CONFIGURE_ALERTS: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/config_alerts'
    },

    GET_UNIVERSE_HEALTH_CHECK: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/health_check'
    },
    IMPORT_UNIVERSE: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/import'
    },
    CREATE_PITR_CONFIG: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/keyspaces/$tableType<[^/]+>/$keyspaceName<[^/]+>/pitr_config'
    },
    DELETE_UNIVERSE_KMS: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/universes/$uniUUID<[^/]+>/kms'
    },
    CREATE_UNIVERSE_KMS: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/kms'
    },
    GET_UNIVERSE_KMS: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/kms'
    },

    GET_MASTER_LEADER: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/leader'
    },
    GET_LIVE_QUERIES: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/live_queries'
    },

    OLD_API_CREATE_UNIVERSE_BACKUP: {
        requestType: ApiRequestType.PUT,
        endpoint: '/universes/$uniUUID<[^/]+>/multi_table_backup'
    },
    GET_UNIVERSE_NAMESPACE: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/namespaces'
    },
    GET_BOOTSTRAP_REQUIREMENT: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/need_bootstrap'
    },
    CREATE_UNIVERSE_NODE_AGENT: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/node_agents'
    },
    PAUSE_UNIVERSE: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/pause'
    },
    RESTORE_PITR_SNAPSHOT: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/pitr'
    },
    GET_PITR_CONFIG: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/pitr_config'
    },
    DELETE_PITR_CONFIG: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/universes/$uniUUID<[^/]+>/pitr_config/$pUUID<[^/]+>'
    },

    GET_REDIS_SERVERS: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/redisservers'
    },
    RESUME_UNIVERSE: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/resume'
    },

    SET_ENCRYPTION_KEY: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/set_key'
    },

    RESET_SLOW_QUERIES: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/universes/$uniUUID<[^/]+>/slow_queries'
    },
    GET_SLOW_QUERIES: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/slow_queries'
    },

    GET_UNIVERSE_NODE_STATUS: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/status'
    },

    CREATE_SUPPORT_BUNDLE: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/support_bundle'
    },
    GET_SUPPORT_BUNDLE: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/support_bundle'
    },
    DELETE_SUPPORT_BUNDLE: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/universes/$uniUUID<[^/]+>/support_bundle/$sbUUID<[^/]+>'
    },
    GET_SUPPORT_BUNDLE_BY_ID: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/support_bundle/$sbUUID<[^/]+>'
    },
    DOWNLOAD_SUPPORT_BUNDLE: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/support_bundle/$sbUUID<[^/]+>/download'
    },

    GET_UNIVERSE_TABLES: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/tables'
    },
    GET_UNIVERSE_TABLE_DETAILS: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/tables/$tableUUID<[^/]+>'
    },
    DELETE_UNIVERSE_TABLE: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/universes/$uniUUID<[^/]+>/tables/$tableUUID<[^/]+>'
    },
    MODIFY_UNIVERSE_TABLE: {
        requestType: ApiRequestType.PUT,
        endpoint: '/universes/$uniUUID<[^/]+>/tables/$tableUUID<[^/]+>'
    },
    BULK_IMPORT_TABLES: {
        requestType: ApiRequestType.PUT,
        endpoint: '/universes/$uniUUID<[^/]+>/tables/$tableUUID<[^/]+>/bulk_import'
    },
    OLD_API_CREATE_TABLE_BACKUP: {
        requestType: ApiRequestType.PUT,
        endpoint: '/universes/$uniUUID<[^/]+>/tables/$tableUUID<[^/]+>/create_backup'
    },
    GET_UNIVERSE_TABLESPACES: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/tablespaces'
    },
    CREATE_UNIVERSE_TABLESPACE: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/tablespaces'
    },
    GET_UNIVERSE_TABLET_SERVERS: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/tablet-servers'
    },
    GET_UNIVERSE_TASKS: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/tasks'
    },

    GET_UNIVERSE_RESOURCE: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/universe_resources'
    },


    MODIFY_BACKUP_STATE: {
        requestType: ApiRequestType.PUT,
        endpoint: '/universes/$uniUUID<[^/]+>/update_backup_state'
    },
    UPDATE_DB_CREDS: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/update_db_credentials'
    },

    MODIFY_UNIVERSE_TLS: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/update_tls'
    },
    UPGRADE_UNIVERSE: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/upgrade'
    },
    UPGRADE_UNIVERSE_CERTS: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/upgrade/certs'
    },
    UPGRADE_UNIVERSE_FINALIZE: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/upgrade/finalize'
    },
    UPGRADE_UNIVERSE_GFLAGS: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/upgrade/gflags'
    },
    UPGRADE_UNIVERSE_KUBE_OVERRIDES: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/upgrade/kubernetes_overrides'
    },
    UPGRADE_UNIVERSE_REBOOT: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/upgrade/reboot'
    },
    UPGRADE_UNIVERSE_RESIZE_NODE: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/upgrade/resize_node'
    },
    UPGRADE_UNIVERSE_RESTART: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/upgrade/restart'
    },
    UPGRADE_UNIVERSE_ROLLBACK: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/upgrade/rollback'
    },
    UPGRADE_UNIVERSE_SOFTWARE: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/upgrade/software'
    },
    UPGRADE_UNIVERSE_SYSTEMD: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/upgrade/systemd'
    },
    UPGRADE_UNIVERSE_THIRDPARTY_SOFTWARE: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/upgrade/thirdparty_software'
    },
    UPGRADE_UNIVERSE_TLS: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/upgrade/tls'
    },
    UPGRADE_UNIVERSE_VM: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/upgrade/vm'
    },
    GET_BACKUP_THROTTLE_PARAMS: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/ybc_throttle_params'
    },
    MODIFY_BACKUP_THROTTLE_PARAMS: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/ybc_throttle_params'
    },
    GET_YQL_SERVERS: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/yqlservers'
    },
    GET_YSQL_SERVERS: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/ysqlservers'
    },
    MODIFY_NODES_BY_NODE_NAME: {
        requestType: ApiRequestType.PUT,
        endpoint: '/universes/$universeUUID<[^/]+>/nodes/$nodeName<[^/]+>'
    },
    GET_NODE_DETAILS: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$universeUUID<[^/]+>/nodes/$nodeName<[^/]+>/details'
    },
    UNIVERSE_CONFIGURE_YCQL: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$univUUID<[^/]+>/configure/ycql'
    },
    UNIVERSE_CONFIGURE_YSQL: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$univUUID<[^/]+>/configure/ysql'
    },
    SYNC_LDAP_ROLES: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$univUUID<[^/]+>/ldap_roles_sync'
    },
    CREATE_UNIVERSE_CLUSTERS: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/clusters'
    },
    CHECK_DUPLICATE_UNIVERSE: { requestType: ApiRequestType.GET, endpoint: '/universes/find' },
    GET_USERS: { requestType: ApiRequestType.GET, endpoint: '/users' },
    CREATE_USER: { requestType: ApiRequestType.POST, endpoint: '/users' },
    GET_USER_BY_ID: {
        requestType: ApiRequestType.GET,
        endpoint: '/users/$uUUID<[^/]+>'
    },
    MODIFY_USER: {
        requestType: ApiRequestType.PUT,
        endpoint: '/users/$uUUID<[^/]+>'
    },
    DELETE_USER: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/users/$uUUID<[^/]+>'
    },

    CHANGE_USER_PASSWORD: {
        requestType: ApiRequestType.PUT,
        endpoint: '/users/$uUUID<[^/]+>/change_password'
    },
    GET_OIDC_AUTH_TOKEN: {
        requestType: ApiRequestType.GET,
        endpoint: '/users/$uUUID<[^/]+>/oidc_auth_token'
    },
    UPDATE_USER_PROFILE: {
        requestType: ApiRequestType.PUT,
        endpoint: '/users/$uUUID<[^/]+>/update_profile'
    },
    VALIDATE_KUBE_OVERRIDES: {
        requestType: ApiRequestType.POST,
        endpoint: '/validate_kubernetes_overrides'
    },
    CREATE_XCLUSTER_REPLICATION: { requestType: ApiRequestType.POST, endpoint: '/xcluster_configs' },
    RESTART_XCLUSTER_REPLICATION: {
        requestType: ApiRequestType.POST,
        endpoint: '/xcluster_configs/$xccUUID<[^/]+>'
    },
    GET_XCLUSTER_REPLICATION: {
        requestType: ApiRequestType.GET,
        endpoint: '/xcluster_configs/$xccUUID<[^/]+>'
    },
    MODIFY_XLCUSTER_REPLICATION: {
        requestType: ApiRequestType.PUT,
        endpoint: '/xcluster_configs/$xccUUID<[^/]+>'
    },
    DELETE_XCLUSTER_REPLICATION: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/xcluster_configs/$xccUUID<[^/]+>'
    },
    GET_XCLUSTER_BOOTSTRAP_REQUIREMENT: {
        requestType: ApiRequestType.POST,
        endpoint: '/xcluster_configs/$xccUUID<[^/]+>/need_bootstrap'
    },
    SYNC_XCLUSTER_REQUIREMENT: {
        requestType: ApiRequestType.POST,
        endpoint: '/xcluster_configs/sync'
    },
    CREATE_NODES_IN_ZONE: {
        requestType: ApiRequestType.POST,
        endpoint: '/zones/$azUUID<[^/]+>/nodes'
    },
    GET_NODES_IN_ZONE: {
        requestType: ApiRequestType.GET,
        endpoint: '/zones/$azUUID<[^/]+>/nodes/list'
    },
    VALIDATE_NODES_IN_ZONE: {
        requestType: ApiRequestType.POST,
        endpoint: '/zones/$azUUID<[^/]+>/nodes/validate'
    },
    GET_GRAFANA_JSON: { requestType: ApiRequestType.GET, endpoint: '/grafana_dashboard' },
    LOGOUT_USER: { requestType: ApiRequestType.GET, endpoint: '/logout' },
    GET_LOGS: { requestType: ApiRequestType.GET, endpoint: '/logs' },
    GET_LOGS_BY_MAX_LINES: {
        requestType: ApiRequestType.GET,
        endpoint: '/logs/$maxLines<[^/]+>'
    },
    GET_AZU_TYPES: {
        requestType: ApiRequestType.GET,
        endpoint: '/metadata/azu_types'
    },
    GET_COLUMN_TYPES: {
        requestType: ApiRequestType.GET,
        endpoint: '/metadata/column_types'
    },
    GET_EBS_TYPES: {
        requestType: ApiRequestType.GET,
        endpoint: '/metadata/ebs_types'
    },
    GET_GCP_TYPES: {
        requestType: ApiRequestType.GET,
        endpoint: '/metadata/gcp_types'
    },
    GET_GFLAG: {
        requestType: ApiRequestType.GET,
        endpoint: '/metadata/version/$version<[^/]+>/gflag'
    },
    GET_GLAGS: {
        requestType: ApiRequestType.GET,
        endpoint: '/metadata/version/$version<[^/]+>/list_gflags'
    },
    VALIDATE_GFLAGS: {
        requestType: ApiRequestType.POST,
        endpoint: '/metadata/version/$version<[^/]+>/validate_gflags'
    },
    GET_YQL_DATA_TYPES: {
        requestType: ApiRequestType.GET,
        endpoint: '/metadata/yql_data_types'
    },
    GET_PROMETHEUS_METRICS: {
        requestType: ApiRequestType.GET,
        endpoint: '/prometheus_metrics'
    },
    GET_RUNTIME_CONFIG_MUTABLE_KEY_INFO: {
        requestType: ApiRequestType.GET,
        endpoint: '/runtime_config/mutable_key_info'
    },
    GET_RUNTIME_CONFIG_MUTABLE_KEY: {
        requestType: ApiRequestType.GET,
        endpoint: '/runtime_config/mutable_keys'
    },
    CREATE_HA_CONFIG: {
        requestType: ApiRequestType.POST,
        endpoint: '/settings/ha/config'
    },
    GET_HA_CONFIG: {
        requestType: ApiRequestType.GET,
        endpoint: '/settings/ha/config'
    },
    DELETE_HA_CONFIG: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/settings/ha/config/$cUUID<[^/]+>'
    },
    MODIFY_HA_CONFIG: {
        requestType: ApiRequestType.PUT,
        endpoint: '/settings/ha/config/$cUUID<[^/]+>'
    },
    GET_HA_CONFIG_BACKUPS: {
        requestType: ApiRequestType.GET,
        endpoint: '/settings/ha/config/$cUUID<[^/]+>/backup/list'
    },
    CREATE_HA_CONFIG_INSTANCE: {
        requestType: ApiRequestType.POST,
        endpoint: '/settings/ha/config/$cUUID<[^/]+>/instance'
    },
    DELETE_HA_CONFIG_INSTANCE: {
        requestType: ApiRequestType.DELETE,
        endpoint: '/settings/ha/config/$cUUID<[^/]+>/instance/$iUUID<[^/]+>'
    },
    PROMOTE_HA_INSTANCE: {
        requestType: ApiRequestType.POST,
        endpoint: '/settings/ha/config/$cUUID<[^/]+>/instance/$iUUID<[^/]+>/promote'
    },
    GET_HA_CONFIG_LOCAL_INSTANCE: {
        requestType: ApiRequestType.GET,
        endpoint: '/settings/ha/config/$cUUID<[^/]+>/instance/local'
    },
    GET_HA_CONFIG_REPLICATION_SCHEDULE: {
        requestType: ApiRequestType.GET,
        endpoint: '/settings/ha/config/$cUUID<[^/]+>/replication_schedule'
    },
    START_HA_REPLICATION_SCHEDULE: {
        requestType: ApiRequestType.PUT,
        endpoint: '/settings/ha/config/$cUUID<[^/]+>/replication_schedule/start'
    },
    STOP_HA_REPLICATION_SCHEDULE: {
        requestType: ApiRequestType.PUT,
        endpoint: '/settings/ha/config/$cUUID<[^/]+>/replication_schedule/stop'
    },
    GENERATE_HA_KEY: {
        requestType: ApiRequestType.GET,
        endpoint: '/settings/ha/generate_key'
    },
    GET_HA_INTERNAL_CONFIG: {
        requestType: ApiRequestType.GET,
        endpoint: '/settings/ha/internal/config'
    },
    DEMOTE_HA_INTERNAL_CONFIG: {
        requestType: ApiRequestType.PUT,
        endpoint: '/settings/ha/internal/config/demote/$timestamp<[^/]+>'
    },
    SYNC_HA_TIMESTAMP: {
        requestType: ApiRequestType.PUT,
        endpoint: '/settings/ha/internal/config/sync/$timestamp<[^/]+>'
    },
    UPLOAD_HA_INTERNAL: {
        requestType: ApiRequestType.POST,
        endpoint: '/settings/ha/internal/upload'
    },
    GET_UNIVERSE_PROXY: {
        requestType: ApiRequestType.GET,
        endpoint: '/universes/$uniUUID<[^/]+>/proxy/[^/]+'
    },
    PERF_ADVISOR_START_MANUALLY: {
        requestType: ApiRequestType.POST,
        endpoint: '/universes/$uniUUID<[^/]+>/start_manually'
    },
    GET_PERF_RECOMENDATION_BY_PAGE: {
        requestType: ApiRequestType.POST,
        endpoint: '/performance_recommendations/page'
    }
} satisfies ApiPermissionMapType;
