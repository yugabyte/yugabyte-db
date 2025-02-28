import React, { FC, useMemo } from "react";
import {
  Box,
  Divider,
  Link,
  MenuItem,
  Paper,
  Typography,
  makeStyles,
  useTheme,
} from "@material-ui/core";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { useTranslation } from "react-i18next";
import ArrowRightIcon from "@app/assets/caret-right-circle.svg";
import { GenericFailure, YBButton, YBInput, YBSelect, YBTable } from "@app/components";
import type { Migration } from "./MigrationOverview";
import { MigrationsGetStarted } from "./MigrationGetStarted";
import RefreshIcon from "@app/assets/refresh.svg";
import SearchIcon from "@app/assets/search.svg";
import { MigrationListSourceDBSidePanel, SourceDBProps } from "./MigrationListSourceDBSidePanel";
import CaretRightIcon from "@app/assets/caret-right.svg";
import CaretDownIcon from "@app/assets/caret-down.svg";
import {
  MigrationListVoyagerSidePanel,
  VoyagerInstanceProps,
} from "./MigrationListVoyagerSidePanel";
import { MigrationListColumns } from "./MigrationListColumns";
import EditIcon from "@app/assets/edit.svg";
import { useLocalStorage } from "react-use";
import { getMemorySizeUnits } from '@app/helpers';
import PlusIcon from '@app/assets/plus.svg';
import { ComplexityComponent } from "./ComplexityComponent";

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
    marginBottom: theme.spacing(2),
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
  noMigrationsPaper: {
    paddingBottom: "10px",
  },
}));

interface MigrationListProps {
  migrationData?: Migration[];
  hasError?: boolean;
  onRefresh?: () => void;
  onSelectMigration: (migration: Migration) => void;
  onNewMigration?: () => void;
}

export const MigrationList: FC<MigrationListProps> = ({
  migrationData: migrationDataProp,
  onSelectMigration,
  hasError,
  onRefresh,
  onNewMigration,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const [selectedMigrations, setSelectedMigrations] = React.useState<number[]>([]);
  const [showArchived, setShowArchived] = React.useState<boolean>(false);
  const [archiveMode, setArchiveMode] = React.useState<boolean>(false);
  const [unarchiveMode, setUnarchiveMode] = React.useState<boolean>(false);

  const [archivedMigrationsLS, setArchivedMigrationsLS] =
    useLocalStorage<Migration["migration_uuid"][]>("archived-migrations");
  const [archivedMigrations, setArchivedMigrations] = React.useState<Migration["migration_uuid"][]>(
    archivedMigrationsLS || []
  );

  React.useEffect(() => {
    setArchivedMigrationsLS(archivedMigrations);
  }, [archivedMigrations]);

  React.useEffect(() => {
    if (!archiveMode) {
      setSelectedMigrations([]);
    }
  }, [archiveMode]);

  React.useEffect(() => {
    if (!unarchiveMode) {
      setSelectedMigrations([]);
    }
  }, [unarchiveMode]);

  const [sourceDBSelection, setSourceDBSelection] = React.useState<SourceDBProps>();
  const [voyagerSelection, setVoyagerSelection] = React.useState<VoyagerInstanceProps>();

  var migrationNewData = migrationDataProp ?? [];

  // Dummy data that will be used if migrations returned are empty.
  // This is passed to the mui-datatable component to prevent it from
  // rendering a message if the table is empty, so we can use our own
  // component which is rendered when there are no migrations.
  var migrationSampleData: (typeof migrationNewData) = [{landing_step: 0}]
//   var migrationSampleData: (typeof migrationNewData) = [
//     {
//       migration_uuid: "c8fc9318-4872-11ee-bdc6-42010a97001c",
//       migration_name: "migration000002",
//       migration_type: "Offline",
//       source_db: {
//         ip: "120.120.120.112",
//         port: "24",
//         engine: "PostgreSQL",
//         version: "15.6",
//         database: "dbpublic-staging",
//         schema: "Public",
//       },
//       voyager: {
//         machine_ip: "120.24.10.224",
//         os: "Ubuntu",
//         avail_disk_bytes: "123456789",
//         export_dir: "file://folder/subfolder/filename",
//         exported_schema_location: "file://folder/subfolder/filename",
//       },
//       target_cluster: {
//         ip: "123.123.123.123",
//         port: "5433",
//         engine: "yugabytedb",
//         version: "11.2-YB-2.23.1.0-b0"
//       },
//       complexity: "LOW",
//       progress: "Assessment",
//       landing_step: 0,
//       start_timestamp: "2024-09-19 14:05:58",
//     },
//     {
//       migration_uuid: "a728a3d7-486c-11ee-8b83-42010a97001a",
//       migration_name: "migration000003",
//       migration_type: "Live",
//       source_db: {
//         ip: "120.120.120.113",
//         port: "24",
//         engine: "Oracle",
//         version: "19c",
//         database: "dbpublic-staging",
//         schema: "Public",
//       },
//       voyager: {
//         machine_ip: "120.24.10.225",
//         os: "Ubuntu",
//         avail_disk_bytes: "123456789",
//         export_dir: "file://folder/subfolder/filename",
//         exported_schema_location: "file://folder/subfolder/filename",
//       },
//       target_cluster: {
//         ip: "123.123.123.123",
//         port: "5433",
//         engine: "yugabytedb",
//         version: "11.2-YB-2.23.1.0-b0"
//     },
//       complexity: "HIGH",
//       progress: "Schema migration",
//       landing_step: 1,
//       start_timestamp: "2024-09-19 14:57:36",
//     },
//   ];

  const getDbEngineString = (engine: string | undefined): string | undefined => {
    switch(engine?.toLowerCase()) {
      case "yugabytedb":
        return "YugabyteDB";
      case "postgresql":
        return "PostgreSQL";
      case "oracle":
        return "Oracle";
      case "mysql":
        return "MySQL";
    };
    return engine;
  };

  if (!migrationDataProp?.length && !hasError) {
    migrationNewData = migrationSampleData;
  }

  const [openColSettings, setOpenColSettings] = React.useState(false);
  const [migrationColSettings, setMigrationColSettings] = React.useState<Record<string, boolean>>(
    {}
  );

  const migrationColumns = (migrations: typeof migrationNewData, mode: boolean) => [
    {
      name: "migration_name",
      label: t("clusterDetail.voyager.migrationID"),
      options: {
        display: true, // always display this
        customBodyRenderLite: (dataIndex: number) => {
          return (
            <Box>
              <Typography variant="body1">{migrations[dataIndex]?.migration_name}</Typography>
              {migrationColSettings.migration_type && (
                <Typography variant="body2">{migrations[dataIndex]?.migration_type}</Typography>
              )}
            </Box>
          );
        },
        setCellHeaderProps: () => ({ style: { padding: "24px 16px" } }),
        setCellProps: () => ({ style: { padding: "16px 16px" } }),
      },
    },
    {
      name: "source_db",
      label: t("clusterDetail.voyager.source"),
      options: {
        display: migrationColSettings.engineVersion || migrationColSettings.host_ip,
        customBodyRender: (sourceDB: (typeof migrationNewData)[number]["source_db"]) => {
          return (
            <Box>
              {migrationColSettings.host_ip && (
                <Link onClick={() => setSourceDBSelection(sourceDB)} >
                  <Typography variant="body2">
                    {sourceDB?.ip}{sourceDB?.port ? ":" + sourceDB.port : ""}
                  </Typography>
                </Link>
              )}
              {migrationColSettings.engineVersion && (
                <Typography variant="body2">
                  {getDbEngineString(sourceDB?.engine)} {sourceDB?.version}
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
      name: "database",
      label: t("clusterDetail.voyager.database"),
      options: {
        display: migrationColSettings.database,
        customBodyRenderLite: (dataIndex: number) => {
          return (
            <Box>
              <Typography variant="body2">{migrations[dataIndex].source_db?.database}</Typography>
            </Box>
          );
        },
        setCellHeaderProps: () => ({ style: { width: "120px", padding: "24px 16px" } }),
        setCellProps: () => ({ style: { padding: "16px 16px" } }),
      },
    },
    {
      name: "schema",
      label: t("clusterDetail.voyager.schema"),
      options: {
        display: migrationColSettings.schema,
        customBodyRenderLite: (dataIndex: number) => {
          return (
            <Box>
              <Typography variant="body2">{migrations[dataIndex].source_db?.schema}</Typography>
            </Box>
          );
        },
        setCellHeaderProps: () => ({ style: { width: "110px", padding: "24px 16px" } }),
        setCellProps: () => ({ style: { padding: "16px 16px" } }),
      },
    },
    {
      name: "voyager",
      label: t("clusterDetail.voyager.voyagerInstance"),
      options: {
        display: migrationColSettings.voyagerInstance || migrationColSettings.machine_ip ||
                 migrationColSettings.os || migrationColSettings.availableDiskSpace ||
                 migrationColSettings.exportDir,
        customBodyRender: (voyager: (typeof migrationNewData)[number]["voyager"]) => {
          return (
            <Box>
              {migrationColSettings.machine_ip && (
                <Link onClick={() => setVoyagerSelection(voyager)}>
                  <Typography variant="body2">
                    {voyager?.machine_ip}
                  </Typography>
                </Link>
              )}
            </Box>
          );
        },
        setCellHeaderProps: () => ({ style: { padding: "24px 16px" } }),
        setCellProps: () => ({ style: { padding: "16px 16px" } }),
      },
    },
    {
      name: "target_cluster",
      label: t("clusterDetail.voyager.targetCluster"),
      options: {
        display: migrationColSettings.targetCluster || migrationColSettings.target_engineVersion ||
                 migrationColSettings.target_host_ip,
        customBodyRender: (targetCluster: (typeof migrationNewData)[number]["target_cluster"]) => {
          return (
            <Box>
              {migrationColSettings.target_host_ip && (
                <Typography variant="body2">
                  {targetCluster?.ip}
                  {targetCluster?.port ? ":" + targetCluster.port : ""}
                </Typography>
              )}
              {migrationColSettings.target_engineVersion && (
                <Typography variant="body2">
                  {getDbEngineString(targetCluster?.engine)} {targetCluster?.version}
                </Typography>
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
              customBodyRender: (complexity: (typeof migrationNewData)[number]["complexity"]) => {
                return <ComplexityComponent complexity={complexity ?? ""}/>;
              },
              setCellHeaderProps: () => ({ style: { width: "120px", padding: "24px 16px" } }),
              setCellProps: () => ({ style: { padding: "16px 16px" } }),
            },
          },
        ]
      : []),
    {
      name: "start_timestamp",
      label: t("clusterDetail.voyager.startedOn"),
      display: migrationColSettings.start_timestamp,
      options: {
        display: migrationColSettings.start_timestamp,
        customBodyRenderLite: (dataIndex: number) => {
          return (
            <Box>
              {migrationColSettings.start_timestamp && (
                <Typography variant="body2">{migrations[dataIndex].start_timestamp}</Typography>
              )}
            </Box>
          );
        },  
        setCellHeaderProps: () => ({ style: { padding: "24px 16px" } }),
        setCellProps: () => ({ style: { padding: "16px 16px" } }),
      },
    },
    {
      name: "progress",
      label: t("clusterDetail.voyager.progress"),
      options: {
        display: true, // always display this one since it has the clickable arrow
        customBodyRenderLite: (dataIndex: number) => {
          return (
            <Box display="flex" alignItems="center" justifyContent="space-between" gridGap={10}>
              <YBBadge
                variant={
                  migrations[dataIndex].progress === "Assessment"
                    ? BadgeVariant.Light
                    : migrations[dataIndex].progress === "Schema migration"
                    ? BadgeVariant.InProgress
                    : migrations[dataIndex].progress === "Data migration"
                    ? BadgeVariant.Info
                    : migrations[dataIndex].progress === "Completed"
                    ? BadgeVariant.Success
                    : undefined
                }
                text={migrations[dataIndex].progress}
                icon={false}
              />
              {!mode && (
                <ArrowRightIcon
                  className={classes.linkBox}
                  onClick={() => onSelectMigration(migrations[dataIndex])}
                />
              )}
            </Box>
          );
        },
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
          const searchFields = [
            migration.migration_name,
            migration.source_db?.ip,
            migration.source_db?.database,
            migration.source_db?.schema,
            migration.target_cluster?.ip,
          ];

          if (!searchFields.some((field) => field?.toLowerCase().includes(searchLower))) {
            return false;
          }
        }

        if (migrationType &&
            migrationType.toLowerCase() !== migration.migration_type?.toLowerCase()) {
          return false;
        }

        if (sourceEngine &&
            sourceEngine.toLowerCase() !== migration.source_db?.engine?.toLowerCase()) {
          return false;
        }

        return true;
      }),
    [search, migrationType, sourceEngine, migrationNewData]
  );

  const migrationData = React.useMemo(
    () => ({
      unarchivedMigrations: !filteredMigrations
        ? []
        : filteredMigrations.filter(
            (m) => !archivedMigrations.find((uuid) => uuid === m.migration_uuid)
          ),
      archivedMigrations: !filteredMigrations
        ? []
        : filteredMigrations.filter((m) =>
            archivedMigrations.find((uuid) => uuid === m.migration_uuid)
          ),
    }),
    [filteredMigrations, archivedMigrations]
  );

  return (
    <Box>
      <Box>
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
                    <MenuItem value="">{t("common.all")}</MenuItem>
                    <Divider className={classes.divider} />
                    <MenuItem value="PostgreSQL">PostgreSQL</MenuItem>
                    <MenuItem value="MySQL">MySQL</MenuItem>
                    <MenuItem value="Oracle">Oracle</MenuItem>
                  </YBSelect>
                </Box>
              </Box>
              <Box>
                {!!migrationDataProp?.length && (
                  <YBButton
                    variant="primary"
                    onClick={onNewMigration}
                    startIcon={<PlusIcon />}
                  >
                    {t('clusterDetail.voyager.gettingStarted.migrateDatabase')}
                  </YBButton>
                )}
                <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={onRefresh}>
                    {t("clusterDetail.performance.actions.refresh")}
                </YBButton>
                <YBButton
                  variant="ghost"
                  startIcon={<EditIcon />}
                  onClick={() => setOpenColSettings(true)}
                >
                  {t("clusterDetail.nodes.editColumns")}
                </YBButton>
              </Box>
            </Box>

            
            {!migrationDataProp?.length && !hasError ? (
                <Paper className={classes.noMigrationsPaper}>
                    <YBTable
                      data={migrationData.unarchivedMigrations}
                      columns={migrationColumns(migrationData.unarchivedMigrations, archiveMode)}
                      options={{
                        customRowRender: () => <></>,
                        pagination: !!(migrationData.unarchivedMigrations.length >= 10),
                        selectableRows: archiveMode ? "multiple" : undefined,
                        rowsSelected: selectedMigrations,
                        onRowSelectionChange: (
                          _currentRowsSelected: { index: number; dataIndex: number }[], // Single item array?
                          _allRowsSelected: { index: number; dataIndex: number }[],
                          rowsSelected: undefined | number[]
                        ) => {
                          setSelectedMigrations(rowsSelected ? [...rowsSelected] : []);
                        },
                      }}
                      touchBorder
                      alternateRowShading={!archiveMode}
                      cellBorder
                      noCellBottomBorder={!archiveMode}
                      noHeaderBottomBorder
                      withBorder={false}
                    />

                    <MigrationsGetStarted onNewMigration={onNewMigration}/>
                </Paper>
            ) : (
                <YBTable
                data={migrationData.unarchivedMigrations}
                columns={migrationColumns(migrationData.unarchivedMigrations, archiveMode)}
                options={{
                  pagination: !!(migrationData.unarchivedMigrations.length >= 10),
                  selectableRows: archiveMode ? "multiple" : undefined,
                  rowsSelected: selectedMigrations,
                  onRowSelectionChange: (
                    _currentRowsSelected: { index: number; dataIndex: number }[], // Single item array?
                    _allRowsSelected: { index: number; dataIndex: number }[],
                    rowsSelected: undefined | number[]
                  ) => {
                    setSelectedMigrations(rowsSelected ? [...rowsSelected] : []);
                  },
                }}
                touchBorder
                alternateRowShading={!archiveMode}
                cellBorder
                noCellBottomBorder={!archiveMode}
                withBorder
              />
            )}
            <MigrationListSourceDBSidePanel
              open={!!sourceDBSelection}
              onClose={() => setSourceDBSelection(undefined)}
              ip={sourceDBSelection?.ip ?? "N/A"}
              port={sourceDBSelection?.port ?? "N/A"}
              engine={sourceDBSelection?.engine ?? "N/A"}
              version={sourceDBSelection?.version ?? "N/A"}
              database={sourceDBSelection?.database ?? "N/A"}
              schema={sourceDBSelection?.schema ?? "N/A"}
            />

            <MigrationListVoyagerSidePanel
              open={!!voyagerSelection}
              onClose={() => setVoyagerSelection(undefined)}
              machine_ip={voyagerSelection?.machine_ip || "N/A"}
              os={voyagerSelection?.os || "N/A"}
              avail_disk_bytes={
                getMemorySizeUnits(parseInt(voyagerSelection?.avail_disk_bytes ?? "")) == '-' ?
                  "N/A" :
                  getMemorySizeUnits(parseInt(voyagerSelection?.avail_disk_bytes ?? ""))}
              export_dir={voyagerSelection?.export_dir || "N/A"}
              exported_schema_location={voyagerSelection?.exported_schema_location || "N/A"}
            />

            <MigrationListColumns
              open={openColSettings}
              onClose={() => setOpenColSettings(false)}
              onUpdateColumns={(cols) => setMigrationColSettings(cols)}
            />

            <Box display="flex" justifyContent="end" gridGap={theme.spacing(1)} mt={2}>
              {!archiveMode && (
                <>
                  {migrationData.unarchivedMigrations.length > 0 && (
                    <YBButton
                      variant="ghost"
                      onClick={() => {
                        setUnarchiveMode(false);
                        setArchiveMode(true);
                      }}
                    >
                      {t("clusterDetail.voyager.archiveMigrations")}
                    </YBButton>
                  )}
                  {!!archivedMigrations.length && (
                    <YBButton
                      variant="ghost"
                      endIcon={showArchived ? <CaretDownIcon /> : <CaretRightIcon />}
                      onClick={() => setShowArchived((s) => !s)}
                    >
                      {showArchived
                        ? t("clusterDetail.voyager.hideArchived")
                        : t("clusterDetail.voyager.showArchived")}
                    </YBButton>
                  )}
                </>
              )}
              {archiveMode && (
                <>
                  <YBButton
                    variant="ghost"
                    onClick={() => {
                      setArchiveMode(false);
                    }}
                  >
                    {t("common.cancel")}
                  </YBButton>
                  <YBButton
                    variant="primary"
                    disabled={!selectedMigrations.length}
                    onClick={() => {
                      setArchivedMigrations((m) => [
                        ...(m ? m : []),
                        ...selectedMigrations.map(
                          (index) => migrationData.unarchivedMigrations[index].migration_uuid
                        ),
                      ]);
                      setShowArchived(true);
                      setArchiveMode(false);
                    }}
                  >
                    {t("clusterDetail.voyager.archiveSelected")}
                  </YBButton>
                </>
              )}
            </Box>
          </Box>
        )}

        {showArchived && !hasError && (
          <Box mt={2}>
            <Box>
              <Typography variant="h4" className={classes.heading}>
                {t("clusterDetail.voyager.archivedMigrations")}
              </Typography>
              <YBTable
                data={migrationData.archivedMigrations}
                columns={migrationColumns(migrationData.archivedMigrations, unarchiveMode)}
                options={{
                  pagination: !!(migrationData.archivedMigrations.length >= 10),
                  selectableRows: unarchiveMode ? "multiple" : undefined,
                  rowsSelected: selectedMigrations,
                  onRowSelectionChange: (
                    _currentRowsSelected: { index: number; dataIndex: number }[], // Single item array?
                    _allRowsSelected: { index: number; dataIndex: number }[],
                    rowsSelected: undefined | number[]
                  ) => {
                    setSelectedMigrations(rowsSelected ? [...rowsSelected] : []);
                  },
                }}
                touchBorder
                alternateRowShading={!unarchiveMode}
                cellBorder
                noCellBottomBorder={!unarchiveMode}
                withBorder
              />
              <Box display="flex" justifyContent="end" gridGap={theme.spacing(1)} mt={2}>
                {!unarchiveMode && migrationData.archivedMigrations.length > 0 && (
                  <YBButton
                    variant="ghost"
                    onClick={() => {
                      setArchiveMode(false);
                      setUnarchiveMode(true);
                    }}
                  >
                    {t("clusterDetail.voyager.unarchiveMigrations")}
                  </YBButton>
                )}
                {unarchiveMode && (
                  <>
                    <YBButton
                      variant="ghost"
                      onClick={() => {
                        setUnarchiveMode(false);
                      }}
                    >
                      {t("common.cancel")}
                    </YBButton>
                    <YBButton
                      variant="primary"
                      disabled={!selectedMigrations.length}
                      onClick={() => {
                        const newArchivedMigraions =
                          archivedMigrations.filter(
                            (uuid) =>
                              uuid &&
                              !selectedMigrations
                                .map(
                                  (index) => migrationData.archivedMigrations[index].migration_uuid
                                )
                                ?.includes(uuid)
                          ) || [];
                        setArchivedMigrations(newArchivedMigraions);
                        setUnarchiveMode(false);
                        if (newArchivedMigraions.length === 0) {
                          setShowArchived(false);
                        }
                      }}
                    >
                      {t("clusterDetail.voyager.unarchiveSelected")}
                    </YBButton>
                  </>
                )}
              </Box>
            </Box>
          </Box>
        )}
      </Box>
    </Box>
  );
};
