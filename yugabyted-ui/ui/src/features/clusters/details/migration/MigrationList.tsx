import React, { FC } from "react";
import { Box, Paper, Typography, makeStyles } from "@material-ui/core";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { useTranslation } from "react-i18next";
import ArrowRightIcon from "@app/assets/caret-right-circle.svg";
import { YBTable } from "@app/components";
import type { Migration } from "./MigrationOverview";
import { MigrationsGetStarted } from "./MigrationGetStarted";
import { migrationPhases } from "./migration";

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
  const className =
    complexity === "Hard"
      ? classes.hardComp
      : complexity === "Medium"
      ? classes.mediumComp
      : complexity === "Easy"
      ? classes.easyComp
      : undefined;

  return <Box className={className}>{complexity || "N/A"}</Box>;
};

const StatusComponent = () => (status: string) => {
  return (
    <Box>
      <YBBadge
        variant={status === "Completed" ? BadgeVariant.Success : BadgeVariant.InProgress}
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
  migrationData: Migration[];
  onSelectMigration: (migration: Migration) => void;
}

export const MigrationList: FC<MigrationListProps> = ({ migrationData, onSelectMigration }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const migrationColumns = [
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
        customBodyRender: (schema: string[]) => schema.join(", "),
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
      name: "source_dbVersion",
      label: t("clusterDetail.voyager.sourceDB"),
      options: {
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
    {
      name: "",
      label: "",
      options: {
        sort: false,
        customBodyRender: ArrowComponent(classes),
      },
    },
  ];

  if (migrationData.length === 0) {
    return <MigrationsGetStarted />;
  }

  return (
    <Paper>
      <Box p={4}>
        <Typography variant="h4" className={classes.heading}>
          {t("clusterDetail.voyager.migrations")}
        </Typography>
        <YBTable
          data={migrationData}
          columns={migrationColumns}
          options={{
            pagination: false,
            rowHover: true,
            onRowClick: (_, { dataIndex }) => onSelectMigration(migrationData[dataIndex]),
          }}
          withBorder={false}
        />
      </Box>
    </Paper>
  );
};
