import React, { FC } from "react";
import { Box, makeStyles, Paper, Theme, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "./MigrationOverview";
import { STATUS_TYPES, YBProgress, YBStatus, YBTable } from "@app/components";

const useStyles = makeStyles((theme) => ({
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: "uppercase",
    textAlign: "start",
  },
  value: {
    paddingTop: theme.spacing(0.36),
    textAlign: "start",
  },
  dividerHorizontal: {
    width: "100%",
    marginTop: theme.spacing(1.5),
    marginBottom: theme.spacing(1.5),
  },
  heading: {
    marginBottom: theme.spacing(5),
  },
}));

const CompletionComponent = (theme: Theme) => (completionPercentage: number) => {
  const statusType =
    completionPercentage === 100
      ? STATUS_TYPES.SUCCESS
      : completionPercentage === 0
      ? STATUS_TYPES.PENDING
      : STATUS_TYPES.IN_PROGRESS;

  return (
    <Box display="flex" alignItems="center" gridGap={theme.spacing(1)} maxWidth={400}>
      <Box style={{ width: "16px" }}>
        <YBStatus type={statusType} />
      </Box>
      {completionPercentage === 100 && <Box>Complete</Box>}
      {completionPercentage === 0 && <Box>Not started</Box>}
      {completionPercentage !== 100 && completionPercentage !== 0 && (
        <>
          <Box>{completionPercentage}%</Box>
          <YBProgress value={completionPercentage} color={theme.palette.primary[500]} />
        </>
      )}
    </Box>
  );
};

interface MigrationExportImportProps {
  heading: string;
  migration: Migration;
}

export const MigrationExportImport: FC<MigrationExportImportProps> = ({ heading, migration }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const migrationProgressData = React.useMemo(
    () => [
      {
        table: "Table A",
        completionPercentage: 100,
      },
      {
        table: "Table B",
        completionPercentage: 60,
      },
      {
        table: "Table C",
        completionPercentage: 10,
      },
      {
        table: "Table D",
        completionPercentage: 0,
      },
    ],
    []
  );

  const totalProgress = Math.round(
    migrationProgressData.reduce((acc, { completionPercentage }) => acc + completionPercentage, 0) /
      migrationProgressData.length
  );

  const migrationProgressColumns = [
    {
      name: "table",
      label: t("clusterDetail.voyager.tableName"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "completionPercentage",
      label: t("clusterDetail.voyager.status"),
      options: {
        customBodyRender: CompletionComponent(theme),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
  ];

  return (
    <Paper>
      <Box p={4}>
        <Typography variant="h4" className={classes.heading}>
          {heading}
        </Typography>
        <Box>
          <Box display="flex" alignItems="center" justifyContent="space-between" mb={1}>
            <Typography variant="body1">
              {t("clusterDetail.voyager.percentCompleted", { percent: totalProgress })}
            </Typography>
            <Box>{t("clusterDetail.voyager.elapsedTime")}: 30m</Box>
          </Box>
          <YBProgress value={totalProgress} />
        </Box>
        <Box mt={6}>
          <YBTable
            data={migrationProgressData}
            columns={migrationProgressColumns}
            options={{
              pagination: true,
            }}
            withBorder={false}
          />
        </Box>
      </Box>
    </Paper>
  );
};
