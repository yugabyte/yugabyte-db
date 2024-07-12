import React, { FC } from "react";
import {
  Badge,
  Box,
  LinearProgress,
  makeStyles,
  Paper,
  Typography,
  useTheme,
} from "@material-ui/core";
import TodoIcon from "@app/assets/todo.svg";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { useTranslation } from "react-i18next";
import { YBAccordion, YBTooltip } from "@app/components";
import RestartIcon from "@app/assets/restart2.svg";
import MigrationAccordionTitle from "../../MigrationAccordionTitle";
import { SchemaAnalysisTabs } from "./SchemaAnalysisTabs";

const useStyles = makeStyles((theme) => ({
  paper: {
    border: "1px solid",
    borderColor: theme.palette.primary[200],
    backgroundColor: theme.palette.primary[100],
    textAlign: "center",
  },
  icon: {
    marginTop: theme.spacing(1),
    flexShrink: 0,
    height: "fit-content",
  },
  badge: {
    height: "32px",
    width: "32px",
    borderRadius: "100%",
  },
  accordionHeader: {
    flex: 1,
    display: "flex",
    alignItems: "center",
    justifyContent: "space-between",
    gap: theme.spacing(1),
  },
  completedTime: {
    color: theme.palette.grey[700],
    fontSize: "0.7rem",
  },
}));

interface SchemaAnalysisProps {}

export const SchemaAnalysis: FC<SchemaAnalysisProps> = ({}) => {
  const classes = useStyles();
  const theme = useTheme();
  const { t } = useTranslation();

  const analysis = [
    {
      id: 1,
      completedOn: "04/01/2024, 16:26 PDT",
      refactorObjectsCount: 0,
      summary: {
        automaticDDLImport: 183,
        manualRefactoring: 13,
        graph: [
          {
            type: "Type",
            automaticDDLImport: 29,
            manualRefactoring: 3,
          },
          {
            type: "Table",
            automaticDDLImport: 78,
            manualRefactoring: 5,
          },
          {
            type: "View",
            automaticDDLImport: 32,
            manualRefactoring: 2,
          },
          {
            type: "Function",
            automaticDDLImport: 27,
            manualRefactoring: 8,
          },
          {
            type: "Triggers",
            automaticDDLImport: 29,
            manualRefactoring: 5,
          },
        ],
      },
    },
    {
      id: 2,
      completedOn: "05/01/2024, 16:26 PDT",
      refactorObjectsCount: 22,
      summary: {
        automaticDDLImport: 183,
        manualRefactoring: 13,
        graph: [
          {
            type: "Type",
            automaticDDLImport: 29,
            manualRefactoring: 3,
          },
          {
            type: "Table",
            automaticDDLImport: 78,
            manualRefactoring: 5,
          },
          {
            type: "View",
            automaticDDLImport: 32,
            manualRefactoring: 2,
          },
          {
            type: "Function",
            automaticDDLImport: 27,
            manualRefactoring: 8,
          },
          {
            type: "Triggers",
            automaticDDLImport: 29,
            manualRefactoring: 5,
          },
        ],
      },
    },
  ];

  return (
    <Box>
      <Paper className={classes.paper}>
        <Box px={2} py={1.5} display="flex" alignItems="center" gridGap={theme.spacing(2)}>
          <YBBadge
            className={classes.badge}
            text=""
            variant={BadgeVariant.InProgress}
            iconComponent={RestartIcon}
          />
          <Typography variant="body2" align="left">
            {t("clusterDetail.voyager.migrateSchema.rerunAnalysis")}
          </Typography>
        </Box>
      </Paper>

      <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)} my={2}>
        {analysis.map((item, index) => (
          <YBAccordion
            key={item.id}
            titleContent={
              <Typography variant="body2" className={classes.accordionHeader}>
                {t("clusterDetail.voyager.migrateSchema.analysis")}
                <Box display="flex" alignItems="center" gridGap={theme.spacing(1)}>
                  <Typography variant="body2" className={classes.completedTime}>
                    {item.completedOn}
                  </Typography>
                  <YBBadge
                    text={t("clusterDetail.voyager.migrateSchema.objectsToRefactorManually", {
                      count: item.refactorObjectsCount,
                    })}
                    variant={
                      item.refactorObjectsCount === 0 ? BadgeVariant.Success : BadgeVariant.Warning
                    }
                  />
                </Box>
              </Typography>
            }
            defaultExpanded={index === analysis.length - 1}
            contentSeparator
          >
            <SchemaAnalysisTabs />
          </YBAccordion>
        ))}
      </Box>
    </Box>
  );
};
