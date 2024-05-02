import React, { FC } from "react";
import { Box, Divider, MenuItem, Paper, Typography, makeStyles } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBSelect } from "@app/components";
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

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(3),
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: "uppercase",
    textAlign: "left",
  },
  selectBox: {
    marginTop: theme.spacing(-0.5),
    minWidth: "220px",
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
  schemaList: readonly string[];
  sqlObjects: ReadonlyArray<{
    objectType: string;
    automaticDDLImport: number;
    manualRefactoring: number;
  }>;
}

export const MigrationAssessmentRefactoring: FC<MigrationAssessmentRefactoringProps> = ({
  schemaList,
  sqlObjects,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [selectedNode, setSelectedNode] = React.useState<string>("");

  return (
    <Paper>
      <Box px={2} py={3}>
        <Box
          display="flex"
          justifyContent="space-between"
          alignItems="center"
          className={classes.heading}
        >
          <Typography variant="h4">
            {t("clusterDetail.voyager.planAndAssess.refactoring.heading")}
          </Typography>
        </Box>
        <Box>
          <Typography variant="body1" className={classes.label}>
            {t("clusterDetail.voyager.planAndAssess.refactoring.schema")}
          </Typography>
          <YBSelect
            className={classes.selectBox}
            value={selectedNode}
            onChange={(e) => setSelectedNode(e.target.value)}
          >
            <MenuItem value="">All</MenuItem>
            <Divider className={classes.divider} />
            {schemaList?.map((schema) => {
              return (
                <MenuItem key={schema} value={schema}>
                  {schema}
                </MenuItem>
              );
            })}
          </YBSelect>
        </Box>

        <Box my={4}>
          <ResponsiveContainer width="100%" height={sqlObjects.length * 60}>
            <BarChart
              data={[...sqlObjects]}
              layout="vertical"
              margin={{
                right: 30,
                left: 20,
              }}
              barCategoryGap={34}
            >
              <CartesianGrid horizontal={false} strokeDasharray="3 3" />
              <XAxis type="number" />
              <YAxis type="category" dataKey="objectType" />
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
                        paddingTop: "10px",
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
        <Box color={payload[0].color}>Automatic DDL Import: {payload[0].value}</Box>
        <Box color={payload[1].color}>Manual Refactoring: {payload[1].value}</Box>
      </Box>
    );
  }

  return null;
};
