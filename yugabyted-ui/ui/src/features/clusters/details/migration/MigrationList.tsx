import React, { FC, useMemo } from "react";
import { Box, Divider, Grid, Link, MenuItem, Typography, makeStyles } from "@material-ui/core";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { useTranslation } from "react-i18next";
import ArrowRightIcon from "@app/assets/caret-right-circle.svg";
import { GenericFailure, YBButton, YBInput, YBSelect, YBTable, YBTooltip } from "@app/components";
import type { Migration } from "./MigrationOverview";
import { MigrationsGetStarted } from "./MigrationGetStarted";
import RefreshIcon from "@app/assets/refresh.svg";
import SearchIcon from "@app/assets/search.svg";
import clsx from "clsx";
import { MigrationListSourceDBSidePanel, SourceDBProps } from "./MigrationListSourceDBSidePanel";
import {
  MigrationListVoyagerSidePanel,
  VoyagerInstanceProps,
} from "./MigrationListVoyagerSidePanel";
import { MigrationListColumns } from "./MigrationListColumns";
import EditIcon from "@app/assets/edit.svg";

const useStyles = makeStyles((theme) => ({
  arrowComponent: {
    textAlign: "end",
    "& svg": {
      marginTop: theme.spacing(0.25),
    },
  },
  complexity: {
    width: 16,
    height: 16,
    borderRadius: "100%",
    border: `1px solid ${theme.palette.grey[300]}`,
  },
  complexityActive: {
    backgroundColor: theme.palette.warning[300],
    borderColor: theme.palette.warning[500],
  },
  heading: {
    marginBottom: theme.spacing(4),
  },
  stat: {
    display: "flex",
    gap: theme.spacing(6),
    paddingRight: theme.spacing(6),
    marginRight: theme.spacing(2),
    borderRight: `1px solid ${theme.palette.grey[300]}`,
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    textTransform: "uppercase",
    textAlign: "left",
  },
  statLabel: {
    marginBottom: theme.spacing(0.75),
  },
  value: {
    color: theme.palette.grey[700],
    paddingTop: theme.spacing(0.57),
    textAlign: "left",
  },
  headerStats: {
    marginBottom: theme.spacing(4),
    display: "flex",
    justifyContent: "space-between",
    gap: theme.spacing(2),
  },
  divider: {
    margin: theme.spacing(1, 0, 1, 0),
  },
  fullWidth: {
    width: "100%",
  },
  linkBox: {
    cursor: "pointer",
  },
}));

const ComplexityComponent = (classes: ReturnType<typeof useStyles>) => (complexity: string) => {
  const complexityL = complexity.toLowerCase();

  const totalComplexityCount = 3;
  const activeComplexityCount = complexityL === "hard" ? 3 : complexityL === "medium" ? 2 : 1;

  return (
    <YBTooltip title={complexity} placement="bottom-start">
      <Box display="flex" alignItems="center" gridGap={6} width="fit-content">
        {Array.from({ length: totalComplexityCount }).map((_, index) => (
          <Box
            className={clsx(
              classes.complexity,
              index < activeComplexityCount && classes.complexityActive
            )}
          />
        ))}
      </Box>
    </YBTooltip>
  );
};

interface MigrationListProps {
  migrationData?: Migration[];
  hasError?: boolean;
  onRefresh?: () => void;
  onSelectMigration: (migration: Migration) => void;
}

export const MigrationList: FC<MigrationListProps> = ({
  migrationData: migrationDataProp,
  /* onSelectMigration, */
  hasError,
  onRefresh,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [sourceDBSelection, setSourceDBSelection] = React.useState<SourceDBProps>();
  const [voyagerSelection, setVoyagerSelection] = React.useState<VoyagerInstanceProps>();

  // const migrationData = migrationDataProp ?? [];

  const migrationNewData = [
    {
      migration_uuid: "c8fc9318-4872-11ee-bdc6-42010a97001c",
      migration_name: "migration000002",
      migration_type: "Offline",
      sourceDB: {
        hostname: "clustername-node-01",
        ip: "120.120.120.112",
        port: "24",
        engine: "PostgreSQL",
        version: "15.6",
        auth: "Login/Password",
        database: "dbpublic-staging",
        schema: "Public",
      },
      voyager: {
        machineIP: "120.24.10.224/32",
        os: "Ubuntu",
        totalDisk: "360 GB",
        usedDisk: "126 GB",
        exportDir: "file://folder/subfolder/filename",
        exportedSchemaLocation: "file://folder/subfolder/filename",
      },
      targetCluster: {
        uuid: "e3067211-7872-99bb-ccc6-92010a977213",
        platform: "YBA",
      },
      complexity: "Easy",
      progress: "Assessment",
      activeIdle: "Idle",
    },
    {
      migration_uuid: "a728a3d7-486c-11ee-8b83-42010a97001a",
      migration_name: "migration000003",
      migration_type: "Live",
      sourceDB: {
        hostname: "clustername-node-02",
        ip: "120.120.120.113",
        port: "24",
        engine: "Oracle",
        version: "19c",
        auth: "Login/Password",
        database: "dbpublic-staging",
        schema: "Public",
      },
      voyager: {
        machineIP: "120.24.10.225/32",
        os: "Ubuntu",
        totalDisk: "360 GB",
        usedDisk: "126 GB",
        exportDir: "file://folder/subfolder/filename",
        exportedSchemaLocation: "file://folder/subfolder/filename",
      },
      targetCluster: {
        uuid: "8b76721c-d872-d9bb-dcc6-d2010a977218",
        platform: "YBM",
      },
      complexity: "Hard",
      progress: "Schema migration",
      activeIdle: "Active",
    },
  ];

  const [openColSettings, setOpenColSettings] = React.useState(false);
  const [migrationColSettings, setMigrationColSettings] = React.useState<Record<string, boolean>>(
    {}
  );

  const migrationColumns = [
    {
      name: "migration_name",
      label: t("clusterDetail.voyager.migration"),
      options: {
        customBodyRenderLite: (dataIndex: number) => {
          return (
            <Box>
              <Typography variant="body1">
                {filteredMigrations[dataIndex].migration_name}
              </Typography>
              {migrationColSettings.migration_type && (
                <Typography variant="body2">
                  {filteredMigrations[dataIndex].migration_type}
                </Typography>
              )}
            </Box>
          );
        },
        setCellHeaderProps: () => ({ style: { padding: "24px 16px" } }),
        setCellProps: () => ({ style: { padding: "16px 16px" } }),
      },
    },
    {
      name: "sourceDB",
      label: t("clusterDetail.voyager.sourceDatabase"),
      options: {
        customBodyRender: (sourceDB: (typeof migrationNewData)[number]["sourceDB"]) => {
          return (
            <Box onClick={() => setSourceDBSelection(sourceDB)} className={classes.linkBox}>
              {migrationColSettings.host_ip && (
                <Typography variant="body2">
                  <Link>
                    {sourceDB.ip}/{sourceDB.port}
                  </Link>
                </Typography>
              )}
              {migrationColSettings.hostname && (
                <Typography variant="body2">{sourceDB.hostname}</Typography>
              )}
              {migrationColSettings.engineVersion && (
                <Typography variant="body2">
                  {sourceDB.engine} {sourceDB.version}
                </Typography>
              )}
              {migrationColSettings.database && (
                <Typography variant="body2">{sourceDB.database}</Typography>
              )}
              {migrationColSettings.schema && (
                <Typography variant="body2">{sourceDB.schema}</Typography>
              )}
            </Box>
          );
        },
        setCellHeaderProps: () => ({ style: { padding: "24px 16px" } }),
        setCellProps: () => ({ style: { padding: "16px 16px" } }),
      },
    },
    {
      name: "voyager",
      label: t("clusterDetail.voyager.voyagerInstance"),
      options: {
        customBodyRender: (voyager: (typeof migrationNewData)[number]["voyager"]) => {
          return (
            <Box onClick={() => setVoyagerSelection(voyager)} className={classes.linkBox}>
              {migrationColSettings.machineIP && (
                <Typography variant="body2">
                  <Link>{voyager.machineIP}</Link>
                </Typography>
              )}
              {migrationColSettings.os && <Typography variant="body2">{voyager.os}</Typography>}
              {migrationColSettings.availableDiskSpace && (
                <Typography variant="body2">
                  {(parseFloat(voyager.totalDisk) - parseFloat(voyager.usedDisk)).toFixed(2)} GB
                  available
                </Typography>
              )}
              {migrationColSettings.exportDir && (
                <Typography variant="body2">{voyager.exportDir}</Typography>
              )}
            </Box>
          );
        },
        setCellHeaderProps: () => ({ style: { padding: "24px 16px" } }),
        setCellProps: () => ({ style: { padding: "16px 16px" } }),
      },
    },
    {
      name: "targetCluster",
      label: t("clusterDetail.voyager.targetCluster"),
      options: {
        customBodyRender: (targetCluster: (typeof migrationNewData)[number]["targetCluster"]) => {
          return (
            <Box>
              {migrationColSettings.clusterUUID && (
                <Typography variant="body2">{targetCluster.uuid}</Typography>
              )}
              {migrationColSettings.ybaYbm && (
                <Typography variant="body2">{targetCluster.platform}</Typography>
              )}
            </Box>
          );
        },
        setCellHeaderProps: () => ({ style: { padding: "24px 16px" } }),
        setCellProps: () => ({ style: { padding: "16px 16px" } }),
      },
    },
    ...(migrationColSettings.complexity
      ? [
          {
            name: "complexity",
            label: t("clusterDetail.voyager.complexity"),
            options: {
              customBodyRender: ComplexityComponent(classes),
              setCellHeaderProps: () => ({ style: { padding: "24px 16px" } }),
              setCellProps: () => ({ style: { padding: "16px 16px" } }),
            },
          },
        ]
      : []),
    {
      name: "progress",
      label: t("clusterDetail.voyager.progress"),
      options: {
        customBodyRender: (progress: (typeof migrationNewData)[number]["progress"]) => (
          <YBBadge
            variant={
              progress === "Assessment"
                ? BadgeVariant.Light
                : progress === "Schema migration"
                ? BadgeVariant.InProgress
                : undefined
            }
            text={progress}
            icon={false}
          />
        ),
        setCellHeaderProps: () => ({ style: { padding: "24px 16px" } }),
        setCellProps: () => ({ style: { padding: "16px 16px" } }),
      },
    },
    {
      name: "activeIdle",
      label: t("clusterDetail.voyager.activeOrIdle"),
      options: {
        customBodyRender: (activeIdle: (typeof migrationNewData)[number]["activeIdle"]) => (
          <Box display="flex" alignItems="center" justifyContent="space-between" gridGap={10}>
            {activeIdle}
            <ArrowRightIcon className={classes.linkBox} />
          </Box>
        ),
        setCellHeaderProps: () => ({ style: { padding: "24px 16px" } }),
        setCellProps: () => ({ style: { padding: "16px 16px" } }),
      },
    },
  ];

  const [search, setSearch] = React.useState<string>("");
  const [migrationType, setMigrationType] = React.useState<string>("");
  const [sourceEngine, setSourceEngine] = React.useState<string>("");

  const filteredMigrations = useMemo(
    () =>
      migrationNewData.filter((migration) => {
        if (search) {
          const searchLower = search.toLowerCase();
          const searchFields = [migration.migration_name, migration.targetCluster.uuid];

          if (!searchFields.some((field) => field.toLowerCase().includes(searchLower))) {
            return false;
          }
        }

        if (migrationType && migrationType !== migration.migration_type) {
          return false;
        }

        if (sourceEngine && sourceEngine !== migration.sourceDB.engine) {
          return false;
        }

        return true;
      }),
    [search, migrationType, sourceEngine]
  );

  if (!migrationDataProp?.length && !hasError) {
    return <MigrationsGetStarted />;
  }

  const completedCount = 1;
  const activeCount = 7;
  const idleCount = 12;
  const errorCount = 3;
  const totalCount = 20;

  return (
    <Box>
      <Box>
        <Box className={classes.headerStats}>
          <Grid container>
            <div className={classes.stat}>
              <div>
                <Typography variant="body1" className={clsx(classes.label, classes.statLabel)}>
                  {t("clusterDetail.voyager.completed")}
                </Typography>
                <Typography variant="h4" className={classes.value}>
                  {completedCount}
                </Typography>
              </div>
            </div>
            <div className={classes.stat}>
              <div>
                <Typography variant="body1" className={clsx(classes.label, classes.statLabel)}>
                  {t("clusterDetail.voyager.active")}
                </Typography>
                <Typography variant="h4" className={classes.value}>
                  {activeCount}
                </Typography>
              </div>
              <div>
                <Typography variant="body1" className={clsx(classes.label, classes.statLabel)}>
                  {t("clusterDetail.voyager.idle")}
                </Typography>
                <Typography variant="h4" className={classes.value}>
                  {idleCount}
                </Typography>
              </div>
            </div>
            <div className={classes.stat}>
              <div>
                <Typography variant="body1" className={clsx(classes.label, classes.statLabel)}>
                  {t("clusterDetail.voyager.error")}
                </Typography>
                <Typography variant="h4" className={classes.value}>
                  {errorCount}
                </Typography>
              </div>
            </div>
            <div>
              <div>
                <Typography variant="body1" className={clsx(classes.label, classes.statLabel)}>
                  {t("clusterDetail.voyager.total")}
                </Typography>
                <Typography variant="h4" className={classes.value}>
                  {totalCount}
                </Typography>
              </div>
            </div>
          </Grid>

          <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={onRefresh}>
            {t("clusterDetail.performance.actions.refresh")}
          </YBButton>
        </Box>

        {hasError ? (
          <GenericFailure />
        ) : (
          <Box>
            <Box display="flex" justifyContent="space-between" my={2} alignItems="end" gridGap={10}>
              <Box display="flex" alignItems="center" gridGap={10} maxWidth={1024} flex={1}>
                <Box flex={3}>
                  <Typography variant="body1" className={classes.label}>
                    {t("clusterDetail.voyager.search")}
                  </Typography>
                  <YBInput
                    className={classes.fullWidth}
                    placeholder={t("clusterDetail.voyager.searchPlaceholder")}
                    InputProps={{
                      startAdornment: <SearchIcon />,
                    }}
                    onChange={(ev) => setSearch(ev.target.value)}
                    value={search}
                  />
                </Box>
                <Box flex={1}>
                  <Typography variant="body1" className={classes.label}>
                    {t("clusterDetail.voyager.migrationType")}
                  </Typography>
                  <YBSelect
                    className={classes.fullWidth}
                    value={migrationType}
                    onChange={(e) => setMigrationType(e.target.value)}
                  >
                    <MenuItem value="">All</MenuItem>
                    <Divider className={classes.divider} />
                    <MenuItem value="Live">Live</MenuItem>
                    <MenuItem value="Offline">Offline</MenuItem>
                  </YBSelect>
                </Box>
                <Box flex={1}>
                  <Typography variant="body1" className={classes.label}>
                    {t("clusterDetail.voyager.sourceEngine")}
                  </Typography>
                  <YBSelect
                    className={classes.fullWidth}
                    value={sourceEngine}
                    onChange={(e) => setSourceEngine(e.target.value)}
                  >
                    <MenuItem value="">All</MenuItem>
                    <Divider className={classes.divider} />
                    <MenuItem value="PostgreSQL">PostgreSQL</MenuItem>
                    <MenuItem value="MySQL">MySQL</MenuItem>
                    <MenuItem value="Oracle">Oracle</MenuItem>
                  </YBSelect>
                </Box>
              </Box>
              <YBButton
                variant="ghost"
                startIcon={<EditIcon />}
                onClick={() => setOpenColSettings(true)}
              >
                {t("clusterDetail.nodes.editColumns")}
              </YBButton>
            </Box>

            <YBTable
              data={filteredMigrations}
              columns={migrationColumns}
              options={{
                pagination: false,
              }}
              touchBorder
              alternateRowShading
              cellBorder
              noCellBottomBorder
              withBorder
            />

            <MigrationListSourceDBSidePanel
              open={!!sourceDBSelection}
              onClose={() => setSourceDBSelection(undefined)}
              hostname={sourceDBSelection?.hostname ?? "N/A"}
              ip={sourceDBSelection?.ip ?? "N/A"}
              port={sourceDBSelection?.port ?? "N/A"}
              engine={sourceDBSelection?.engine ?? "N/A"}
              version={sourceDBSelection?.version ?? "N/A"}
              auth={sourceDBSelection?.auth ?? "N/A"}
              database={sourceDBSelection?.database ?? "N/A"}
              schema={sourceDBSelection?.schema ?? "N/A"}
            />

            <MigrationListVoyagerSidePanel
              open={!!voyagerSelection}
              onClose={() => setVoyagerSelection(undefined)}
              machineIP={voyagerSelection?.machineIP ?? "N/A"}
              os={voyagerSelection?.os ?? "N/A"}
              totalDisk={voyagerSelection?.totalDisk ?? "N/A"}
              usedDisk={voyagerSelection?.usedDisk ?? "N/A"}
              exportDir={voyagerSelection?.exportDir ?? "N/A"}
              exportedSchemaLocation={voyagerSelection?.exportedSchemaLocation ?? "N/A"}
            />

            <MigrationListColumns
              open={openColSettings}
              onClose={() => setOpenColSettings(false)}
              onUpdateColumns={(cols) => setMigrationColSettings(cols)}
            />
          </Box>
        )}
      </Box>
    </Box>
  );
};
