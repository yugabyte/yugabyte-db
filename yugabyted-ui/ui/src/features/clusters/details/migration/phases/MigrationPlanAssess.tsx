import React, { FC } from "react";
import { Box, makeStyles, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "../MigrationOverview";
import { ErrorRounded, InfoOutlined } from "@material-ui/icons";
import { YBAccordion, YBTable } from "@app/components";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(5),
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

interface MigrationPlanAssessProps {
  heading: string;
  migration: Migration;
  phase: number;
}

export const MigrationPlanAssess: FC<MigrationPlanAssessProps> = ({ heading }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const complexityData = React.useMemo(
    () => [
      {
        schema: "yugabyted",
        sqlObjects: 128,
        tableCount: 77,
        totalDatasize: 32,
        complexity: "Hard",
      },
      {
        schema: "yugabyted2",
        sqlObjects: 44,
        tableCount: 33,
        totalDatasize: 22,
        complexity: "Medium",
      },
      {
        schema: "yugabyted3",
        sqlObjects: 148,
        tableCount: 57,
        totalDatasize: 72,
        complexity: "Easy",
      },
      {
        schema: "yugabyted4",
        sqlObjects: 100,
        tableCount: 50,
        totalDatasize: 25,
        complexity: "Easy",
      },
    ],
    []
  );

  const complexityColumns = [
    {
      name: "schema",
      label: t("clusterDetail.voyager.schema"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "sqlObjects",
      label: t("clusterDetail.voyager.sqlObjectCount"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "tableCount",
      label: t("clusterDetail.voyager.tableCount"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "totalDatasize",
      label: t("clusterDetail.voyager.totalDatasize"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
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
  ];

  const suggestions = ["XYZ is deprecated, use ABC instead", "Use KLM for better compatibility"];

  const errors = ["Invalid character at line 22, character 63", "Duplicate fields detected"];

  return (
    <Box>
      <Typography variant="h4" className={classes.heading}>
        {heading}
      </Typography>
      <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
        <YBAccordion
          titleContent={
            <AccordionTitleCount
              title={t("clusterDetail.voyager.topSuggestions")}
              count={suggestions.length}
            />
          }
          defaultExpanded
        >
          <Box display="flex" gridGap={theme.spacing(1)} flexDirection="column">
            {suggestions.map((suggestion) => (
              <Box display="flex" alignItems="center" gridGap={theme.spacing(1)}>
                <InfoOutlined color="primary" />
                <Typography variant="body2">{suggestion}</Typography>
              </Box>
            ))}
          </Box>
        </YBAccordion>

        <YBAccordion
          titleContent={
            <AccordionTitleCount
              title={t("clusterDetail.voyager.topErrors")}
              count={errors.length}
              color={theme.palette.error[100]}
            />
          }
          defaultExpanded
        >
          <Box display="flex" gridGap={theme.spacing(1)} flexDirection="column">
            {errors.map((error) => (
              <Box display="flex" alignItems="center" gridGap={theme.spacing(1)}>
                <ErrorRounded color="error" />
                <Typography variant="body2">{error}</Typography>
              </Box>
            ))}
          </Box>
        </YBAccordion>

        <YBAccordion
          titleContent={t("clusterDetail.voyager.migrationComplexityOverview")}
          defaultExpanded
        >
          <Box flex={1} px={2}>
            <YBTable
              data={complexityData}
              columns={complexityColumns}
              options={{
                pagination: false,
              }}
              withBorder={false}
            />
          </Box>
        </YBAccordion>
      </Box>
    </Box>
  );
};

const AccordionTitleCount = ({
  title,
  count,
  color,
}: {
  title: string;
  count?: number;
  color?: string;
}) => {
  const theme = useTheme();

  return (
    <Box display="flex" alignItems="center" gridGap={theme.spacing(1)} flexGrow={1}>
      {title}
      {count && (
        <Box
          borderRadius={theme.shape.borderRadius}
          bgcolor={color || theme.palette.info[100]}
          fontSize="12px"
          fontWeight={400}
          px="5px"
          py="2px"
        >
          2
        </Box>
      )}
    </Box>
  );
};
