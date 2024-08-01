import React, { FC, useMemo } from "react";
import { Box, Divider, Paper, Typography, makeStyles } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import {
  Bar,
  BarChart,
  CartesianGrid,
  LabelList,
  Legend,
  ResponsiveContainer,
  Tooltip,
  TooltipProps,
  XAxis,
  YAxis,
} from "recharts";
import type { NameType, ValueType } from "recharts/types/component/DefaultTooltipContent";
import type { RefactoringCount, UnsupportedSqlInfo } from "@app/api/src";
import { MigrationAssessmentRefactoringTable } from "./AssessmentRefactoringTable";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(3),
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.25),
    textTransform: "uppercase",
    textAlign: "left",
  },
  fullWidth: {
    width: "100%",
  },
  divider: {
    margin: theme.spacing(1, 0, 1, 0),
  },
  tooltip: {
    backgroundColor: theme.palette.common.white,
    padding: theme.spacing(1),
    borderRadius: theme.shape.borderRadius,
    boxShadow: theme.shadows[2],
  },
}));

interface MigrationAssessmentRefactoringProps {
  sqlObjects: RefactoringCount[] | undefined;
  unsupportedDataTypes: UnsupportedSqlInfo[] | undefined;
  unsupportedFeatures: UnsupportedSqlInfo[] | undefined;
  unsupportedFunctions: UnsupportedSqlInfo[] | undefined;
}

export const MigrationAssessmentRefactoring: FC<MigrationAssessmentRefactoringProps> = ({
  sqlObjects,
  unsupportedDataTypes,
  unsupportedFeatures,
  unsupportedFunctions,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const graphData = useMemo(() => {
    if (!sqlObjects) {
      return [];
    }

    return sqlObjects
      .filter(({ automatic, manual }) => (automatic ?? 0) + (manual ?? 0) > 0)
      .map(({ sql_object_type, automatic, manual }) => {
        return {
          objectType:
            sql_object_type
              ?.replace(/^_+|_+$/g, "")
              .trim()
              .toUpperCase()
              .replaceAll("_", " ") || "",
          automaticDDLImport: automatic ?? 0,
          manualRefactoring: manual ?? 0,
        };
      });
  }, [sqlObjects]);

  const barCategoryGap = 34;
  const barSize = 22;
  const graphHeight = graphData.length * 60 + barCategoryGap + barSize;

  if (
    !graphData.length &&
    !unsupportedDataTypes?.length &&
    !unsupportedFeatures?.length &&
    !unsupportedFunctions?.length
  ) {
    return null;
  }

  return (
    <Paper>
      <Box px={2} py={3}>
        <Box
          display="flex"
          justifyContent="space-between"
          alignItems="center"
          className={classes.heading}
        >
          <Typography variant="h5">
            {t("clusterDetail.voyager.planAndAssess.refactoring.heading")}
          </Typography>
        </Box>

        {graphData.length ? (
          <Box my={4}>
            <ResponsiveContainer width="100%" height={graphHeight}>
              <BarChart
                data={graphData}
                layout="vertical"
                margin={{
                  right: 30,
                  left: 50,
                }}
                barCategoryGap={barCategoryGap}
                barSize={barSize}
              >
                <CartesianGrid horizontal={false} strokeDasharray="3 3" />
                <XAxis type="number" />
                <YAxis
                  type="category"
                  dataKey="objectType"
                  textAnchor="start"
                  dx={-90}
                  tickLine={false}
                  axisLine={{ stroke: "#FFFFFF00" }}
                />
                <Tooltip content={<CustomTooltip />} />
                <Bar
                  dataKey="automaticDDLImport"
                  fill="#2FB3FF"
                  stackId="stack"
                  isAnimationActive={false}
                >
                  <LabelList
                    dataKey="automaticDDLImport"
                    position="insideRight"
                    style={{ fill: "black" }}
                    {...{
                      formatter: (value: number) => value || null,
                    }}
                  />
                </Bar>
                <Bar
                  dataKey="manualRefactoring"
                  fill="#FFA400"
                  stackId="stack"
                  isAnimationActive={false}
                >
                  <LabelList
                    dataKey="manualRefactoring"
                    position="insideRight"
                    style={{ fill: "black" }}
                    {...{
                      formatter: (value: number) => value || null,
                    }}
                  />
                </Bar>
                <Legend
                  align="left"
                  content={({ payload }) => {
                    if (!payload) {
                      return null;
                    }

                    const formatter = (value: string) =>
                      value
                        .split(/(?=[A-Z][a-z])|(?<=[a-z])(?=[A-Z])/)
                        .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
                        .join(" ");

                    return (
                      <ul
                        style={{
                          listStyleType: "none",
                          display: "flex",
                          gap: "20px",
                          paddingLeft: "70px",
                        }}
                      >
                        {payload.map((entry) => (
                          <li
                            key={entry.value}
                            style={{ display: "flex", alignItems: "center", gap: "10px" }}
                          >
                            <div
                              style={{
                                height: "16px",
                                width: "16px",
                                borderRadius: "2px",
                                backgroundColor: entry.color,
                              }}
                            />
                            <div style={{ color: "#4E5F6D" }}>{formatter(entry.value)}</div>
                          </li>
                        ))}
                      </ul>
                    );
                  }}
                />
              </BarChart>
            </ResponsiveContainer>
          </Box>
        ) : null}

        {unsupportedDataTypes?.length ||
        unsupportedFeatures?.length ||
        unsupportedFunctions?.length ? (
          <>
            <Divider />

            <Box my={3}>
              <Typography variant="h5">
                {t("clusterDetail.voyager.planAndAssess.refactoring.conversionIssues")}
              </Typography>
            </Box>

            <Box display="flex" flexDirection="column" gridGap={20}>
              {unsupportedDataTypes?.length && (
                <MigrationAssessmentRefactoringTable
                  data={unsupportedDataTypes}
                  tableHeader={t(
                    "clusterDetail.voyager.planAndAssess.refactoring.unsupportedDataType"
                  )}
                />
              )}
              {unsupportedFeatures?.length && (
                <MigrationAssessmentRefactoringTable
                  data={unsupportedFeatures}
                  tableHeader={t(
                    "clusterDetail.voyager.planAndAssess.refactoring.unsupportedFeature"
                  )}
                />
              )}
              {unsupportedFunctions?.length && (
                <MigrationAssessmentRefactoringTable
                  data={unsupportedFunctions}
                  tableHeader={t(
                    "clusterDetail.voyager.planAndAssess.refactoring.unsupportedFunction"
                  )}
                />
              )}
            </Box>
          </>
        ) : null}
      </Box>
    </Paper>
  );
};

const CustomTooltip = ({ active, payload, label }: TooltipProps<ValueType, NameType>) => {
  const classes = useStyles();

  if (active && payload && payload.length) {
    return (
      <Box className={classes.tooltip}>
        <Box mb={0.5}>
          <Typography>{label}</Typography>
        </Box>
        {payload[0]?.value ? (
          <Box color={payload[0].color}>Automatic DDL Import: {payload[0].value}</Box>
        ) : null}
        {payload[1]?.value ? (
          <Box color={payload[1].color}>Manual Refactoring: {payload[1].value}</Box>
        ) : null}
      </Box>
    );
  }

  return null;
};
