import React, { FC } from "react";
import { Box, makeStyles, Theme, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "../MigrationOverview";
import { STATUS_TYPES, YBAccordion, YBProgress, YBStatus, YBTable } from "@app/components";

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
    <Box display="flex" alignItems="center" maxWidth={400}>
      <Box mr={0.5}>
        <YBStatus type={statusType} />
      </Box>
      {completionPercentage === 100 && <Box>Complete</Box>}
      {completionPercentage === 0 && <Box>Not started</Box>}
      {completionPercentage !== 100 && completionPercentage !== 0 && (
        <>
          <Box mr={1}>{completionPercentage}%</Box>
          <YBProgress value={completionPercentage} color={theme.palette.primary[500]} />
        </>
      )}
    </Box>
  );
};

interface MigrationProps {
  heading: string;
  migration: Migration;
  step: number;
}

export const MigrationData: FC<MigrationProps> = ({ heading }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const migrationProgressData = React.useMemo(
    () => [
      {
        table: "Table A",
        importPercentage: 100,
        exportPercentage: 100,
      },
      {
        table: "Table B",
        importPercentage: 62,
        exportPercentage: 100,
      },
      {
        table: "Table C",
        importPercentage: 30,
        exportPercentage: 100,
      },
      {
        table: "Table D",
        importPercentage: 0,
        exportPercentage: 100,
      },
    ],
    []
  );

  const totalProgress = Math.round(
    migrationProgressData.reduce(
      (acc, { importPercentage, exportPercentage }) => acc + importPercentage + exportPercentage,
      0
    ) /
      (migrationProgressData.length * 2)
  );

  const migrationImportColumns = [
    {
      name: "table",
      label: t("clusterDetail.voyager.tableName"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "importPercentage",
      label: t("clusterDetail.voyager.status"),
      options: {
        customBodyRender: CompletionComponent(theme),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
  ];

  const migrationExportColumns = [
    {
      name: "table",
      label: t("clusterDetail.voyager.tableName"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "exportPercentage",
      label: t("clusterDetail.voyager.status"),
      options: {
        customBodyRender: CompletionComponent(theme),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
  ];

  return (
    <Box>
      <Typography variant="h4" className={classes.heading}>
        {heading}
      </Typography>
      <Box>
        <Box display="flex" alignItems="center" justifyContent="space-between" mb={1}>
          <Typography variant="body1">
            {t("clusterDetail.voyager.percentCompleted", { percent: totalProgress })}
          </Typography>
          <Box>{t("clusterDetail.voyager.elapsedTime")}: 30 minutes</Box>
        </Box>
        <YBProgress value={totalProgress} />
      </Box>
      <Box mt={6} display="flex" gridGap={theme.spacing(2)} flexDirection="column">
        <YBAccordion
          titleContent={
            <AccordionTitleComponent
              title={t("clusterDetail.voyager.exportData")}
              status={STATUS_TYPES.SUCCESS}
            />
          }
          defaultExpanded
        >
          <Box mt={2} flex={1}>
            <YBTable
              data={migrationProgressData}
              columns={migrationExportColumns}
              options={{
                pagination: true,
              }}
              withBorder={false}
            />
          </Box>
        </YBAccordion>
        <YBAccordion
          titleContent={
            <AccordionTitleComponent
              title={t("clusterDetail.voyager.importData")}
              status={STATUS_TYPES.IN_PROGRESS}
            />
          }
          defaultExpanded
        >
          <Box mt={2} flex={1}>
            <YBTable
              data={migrationProgressData}
              columns={migrationImportColumns}
              options={{
                pagination: true,
              }}
              withBorder={false}
            />
          </Box>
        </YBAccordion>
      </Box>
    </Box>
  );
};

const AccordionTitleComponent: React.FC<{ title: string; status?: STATUS_TYPES }> = ({
  title,
  status,
}) => {
  const theme = useTheme();

  return (
    <Box display="flex" alignItems="center" gridGap={theme.spacing(1.2)} flexGrow={1}>
      <Box mt={0.2}>{title}</Box>
      {status && <YBStatus type={status} />}
    </Box>
  );
};
