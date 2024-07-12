import React, { FC, useMemo } from "react";
import { Box, Typography, makeStyles } from "@material-ui/core";
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

interface RefactoringCount {
  manual?: number;
  automatic?: number;
}

interface RecommendedRefactoringGraph {
  sql_type?: RefactoringCount;
  table?: RefactoringCount;
  view?: RefactoringCount;
  _function?: RefactoringCount;
  triggers?: RefactoringCount;
}

const useStyles = makeStyles((theme) => ({
  tooltip: {
    backgroundColor: theme.palette.common.white,
    padding: theme.spacing(1),
    borderRadius: theme.shape.borderRadius,
    boxShadow: theme.shadows[2],
  },
}));

interface RefactoringGraphProps {
  sqlObjects: RecommendedRefactoringGraph | undefined;
}

export const RefactoringGraph: FC<RefactoringGraphProps> = ({ sqlObjects }) => {
  const graphData = useMemo(() => {
    if (!sqlObjects) {
      return [];
    }

    return Object.entries(sqlObjects)
      .filter(([_, value]) => (value?.automatic ?? 0) + (value?.manual ?? 0) > 0)
      .map(([key, value]) => {
        return {
          objectType: key
            .replace(/^_+|_+$/g, "")
            .trim()
            .toUpperCase()
            .replaceAll("_", " "),
          automaticDDLImport: value?.automatic ?? 0,
          manualRefactoring: value?.manual ?? 0,
        };
      });
  }, [sqlObjects]);

  const barCategoryGap = 34;
  const barSize = 22;
  const graphHeight = graphData.length * 60 + barCategoryGap + barSize;

  return (
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
          <YAxis type="category" dataKey="objectType" textAnchor="start" dx={-90} />
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
          <Bar dataKey="manualRefactoring" fill="#FFA400" stackId="stack" isAnimationActive={false}>
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
