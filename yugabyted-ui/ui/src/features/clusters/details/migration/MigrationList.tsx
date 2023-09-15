import React, { FC } from "react";
import { Box, Paper, Typography, makeStyles, useTheme } from "@material-ui/core";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { useTranslation } from "react-i18next";
import ArrowRightIcon from "@app/assets/caret-right-circle.svg";
import { GenericFailure, YBButton, YBTable } from "@app/components";
import type { Migration } from "./MigrationOverview";
import { MigrationsGetStarted } from "./MigrationGetStarted";
import { migrationPhases } from "./migration";
import CaretRightIcon from "@app/assets/caret-right.svg";
import CaretDownIcon from "@app/assets/caret-down.svg";
import RefreshIcon from "@app/assets/refresh.svg";
import { useLocalStorage } from "react-use";

const useStyles = makeStyles((theme) => ({
  arrowComponent: {
    textAlign: "end",
    "& svg": {
      marginTop: theme.spacing(0.25),
    },
  },
  hardComp: {
    color: theme.palette.error.main,
  },
  mediumComp: {
    color: theme.palette.warning[700],
  },
  easyComp: {
    color: theme.palette.success.main,
  },
  heading: {
    marginBottom: theme.spacing(4),
  },
}));

const ComplexityComponent = (classes: ReturnType<typeof useStyles>) => (complexity: string) => {
  const complexityL = complexity.toLowerCase();

  const className =
    complexityL === "hard"
      ? classes.hardComp
      : complexityL === "medium"
      ? classes.mediumComp
      : complexityL === "easy"
      ? classes.easyComp
      : undefined;

  return <Box className={className}>{complexity || "N/A"}</Box>;
};

const StatusComponent = () => (status: string) => {
  return (
    <Box>
      <YBBadge
        variant={status === "Complete" ? BadgeVariant.Success : BadgeVariant.InProgress}
        text={status}
        icon={false}
      />
    </Box>
  );
};

const ArrowComponent = (classes: ReturnType<typeof useStyles>) => () => {
  return (
    <Box className={classes.arrowComponent}>
      <ArrowRightIcon />
    </Box>
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
  onSelectMigration,
  hasError,
  onRefresh,
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

  const migrationData = React.useMemo(
    () => ({
      unarchivedMigrations: !migrationDataProp
        ? []
        : migrationDataProp.filter(
            (m) => !archivedMigrations.find((uuid) => uuid === m.migration_uuid)
          ),
      archivedMigrations: !migrationDataProp
        ? []
        : migrationDataProp.filter((m) =>
            archivedMigrations.find((uuid) => uuid === m.migration_uuid)
          ),
    }),
    [migrationDataProp, archivedMigrations]
  );

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

  const migrationUnarchivedColumns = [
    {
      name: "migration_name",
      label: t("clusterDetail.voyager.name"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "database_name",
      label: t("clusterDetail.voyager.database"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "schema_name",
      label: t("clusterDetail.voyager.schema"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({
          style: { padding: "8px 16px", maxWidth: 120, wordBreak: "break-word" },
        }),
      },
    },
    {
      name: "complexity",
      label: t("clusterDetail.voyager.complexity"),
      options: {
        customBodyRender: ComplexityComponent(classes),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "source_db",
      label: t("clusterDetail.voyager.sourceDB"),
      options: {
        customBodyRender: (sourceDB: string) => sourceDB.toUpperCase(),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({
          style: { padding: "8px 16px", maxWidth: 120, wordBreak: "break-word" },
        }),
      },
    },
    {
      name: "migration_phase",
      label: t("clusterDetail.voyager.phase"),
      options: {
        customBodyRender: (phase: number) => migrationPhases[phase],
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "status",
      label: t("clusterDetail.voyager.status"),
      options: {
        customBodyRender: StatusComponent(),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "invocation_timestamp",
      label: t("clusterDetail.voyager.startedOn"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    ...(!archiveMode
      ? [
          {
            name: "",
            label: "",
            options: {
              sort: false,
              customBodyRender: ArrowComponent(classes),
            },
          },
        ]
      : []),
  ];

  const migrationArchivedColumns = [
    {
      name: "migration_name",
      label: t("clusterDetail.voyager.name"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "database_name",
      label: t("clusterDetail.voyager.database"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "schema_name",
      label: t("clusterDetail.voyager.schema"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({
          style: { padding: "8px 16px", maxWidth: 120, wordBreak: "break-word" },
        }),
      },
    },
    {
      name: "complexity",
      label: t("clusterDetail.voyager.complexity"),
      options: {
        customBodyRender: ComplexityComponent(classes),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "source_db",
      label: t("clusterDetail.voyager.sourceDB"),
      options: {
        customBodyRender: (sourceDB: string) => sourceDB.toUpperCase(),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({
          style: { padding: "8px 16px", maxWidth: 120, wordBreak: "break-word" },
        }),
      },
    },
    {
      name: "migration_phase",
      label: t("clusterDetail.voyager.phase"),
      options: {
        customBodyRender: (phase: number) => migrationPhases[phase],
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "status",
      label: t("clusterDetail.voyager.status"),
      options: {
        customBodyRender: StatusComponent(),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "invocation_timestamp",
      label: t("clusterDetail.voyager.startedOn"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    ...(!unarchiveMode
      ? [
          {
            name: "",
            label: "",
            options: {
              sort: false,
              customBodyRender: ArrowComponent(classes),
            },
          },
        ]
      : []),
  ];

  if (!migrationDataProp?.length) {
    return <MigrationsGetStarted />;
  }

  return (
    <Box>
      <Paper>
        <Box p={4}>
          <Box display="flex" justifyContent="space-between" alignItems="start">
            <Typography variant="h4" className={classes.heading}>
              {t("clusterDetail.voyager.migrations")}
            </Typography>
            <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={onRefresh}>
              {t("clusterDetail.performance.actions.refresh")}
            </YBButton>
          </Box>
          {hasError ? (
            <GenericFailure />
          ) : (
            <Box>
              <YBTable
                data={migrationData.unarchivedMigrations}
                columns={migrationUnarchivedColumns}
                options={{
                  pagination: false,
                  rowHover: !archiveMode && !!migrationData.unarchivedMigrations.length,
                  selectableRows: archiveMode ? "multiple" : undefined,
                  rowsSelected: selectedMigrations,
                  onRowSelectionChange: (
                    _currentRowsSelected: { index: number; dataIndex: number }[], // Single item array?
                    _allRowsSelected: { index: number; dataIndex: number }[],
                    rowsSelected: undefined | number[]
                  ) => {
                    setSelectedMigrations(rowsSelected ? [...rowsSelected] : []);
                  },
                  onRowClick: !archiveMode
                    ? (_, { dataIndex }) =>
                        onSelectMigration(migrationData.unarchivedMigrations[dataIndex])
                    : undefined,
                }}
                withBorder={false}
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
        </Box>
      </Paper>

      {showArchived && (
        <Box mt={2}>
          <Paper>
            <Box p={4}>
              <Box>
                <Typography variant="h4" className={classes.heading}>
                  {t("clusterDetail.voyager.archivedMigrations")}
                </Typography>
                <YBTable
                  data={migrationData.archivedMigrations}
                  columns={migrationArchivedColumns}
                  options={{
                    pagination: false,
                    rowHover: !unarchiveMode && !!migrationData.archivedMigrations.length,
                    selectableRows: unarchiveMode ? "multiple" : undefined,
                    rowsSelected: selectedMigrations,
                    onRowSelectionChange: (
                      _currentRowsSelected: { index: number; dataIndex: number }[], // Single item array?
                      _allRowsSelected: { index: number; dataIndex: number }[],
                      rowsSelected: undefined | number[]
                    ) => {
                      setSelectedMigrations(rowsSelected ? [...rowsSelected] : []);
                    },
                    onRowClick: !unarchiveMode
                      ? (_, { dataIndex }) =>
                          onSelectMigration(migrationData.archivedMigrations[dataIndex])
                      : undefined,
                  }}
                  withBorder={false}
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
                                !selectedMigrations
                                  .map(
                                    (index) =>
                                      migrationData.archivedMigrations[index].migration_uuid
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
          </Paper>
        </Box>
      )}
    </Box>
  );
};
