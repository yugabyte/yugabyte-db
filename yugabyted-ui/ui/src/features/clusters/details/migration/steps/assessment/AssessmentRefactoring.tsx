import React, { FC, Fragment, useMemo } from "react";
import {
  Box,
  Divider,
  Paper,
  TableCell,
  TableRow,
  Typography,
  makeStyles,
} from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBButton, YBTable } from "@app/components";
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
import clsx from "clsx";
import ExpandIcon from "@app/assets/expand.svg";
import CollpaseIcon from "@app/assets/collapse.svg";
import ArrowRightIcon from "@app/assets/caret-right-circle.svg";
import PlusIcon from "@app/assets/plus_icon.svg";
import MinusIcon from "@app/assets/minus_icon.svg";
import { MigrationRefactoringSidePanel } from "./AssessmentRefactoringSidePanel";

const ENABLE_MORE_DETAILS = false;

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
  arrowComponent: {
    textAlign: "end",
    cursor: "pointer",
    "& svg": {
      marginTop: theme.spacing(0.25),
    },
  },
  tableCell: {
    padding: theme.spacing(1, 2),
    maxWidth: 120,
    wordBreak: "break-word",
  },
  rowTableCell: {
    borderBottom: "unset",
  },
  w10: {
    width: "10px",
  },
  innerTable: {
    borderRadius: theme.shape.borderRadius,
    backgroundColor: theme.palette.background.default,
    marginLeft: theme.spacing(5),
    "& .MuiPaper-root": {
      backgroundColor: "transparent !important",
    },
  },
  innerTableParent: {
    padding: theme.spacing(0.5, 0, 1, 0),
  },
}));

export type RefactoringDataItems = ReadonlyArray<{
  datatype: string;
  objects: ReadonlyArray<{
    filePath: string;
    sql: string;
    type: string;
    ack?: boolean;
  }>;
}>;

interface MigrationAssessmentRefactoringProps {
  sqlObjects: ReadonlyArray<{
    ObjectType: string;
    TotalCount: number;
    ObjectNames: string;
  }>;
  unsupportedDataTypes: null | ReadonlyArray<{
    DataType: string;
    ObjectNames: string[];
  }>;
  unsupportedFeatures: null | ReadonlyArray<{
    FeatureName: string;
    ObjectNames: string[];
  }>;
}

const getRowCellComponent = (
  displayedRows: RefactoringDataItems,
  expanded: boolean[],
  classes: ReturnType<typeof useStyles>
) => {
  const { t } = useTranslation();

  const innerColumns = [
    {
      name: "objecttype",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.objectType"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "6px 16px" } }),
        setCellProps: () => ({ style: { padding: "6px 16px" } }),
      },
    },
    {
      name: "fileDirectory",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.fileDirectory"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "6px 16px" } }),
        setCellProps: () => ({ style: { padding: "6px 16px" } }),
      },
    },
    {
      name: "totalObjects",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.totalObjects"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "6px 16px" } }),
        setCellProps: () => ({ style: { padding: "6px 16px" } }),
      },
    },
    {
      name: "acknowledgedObjects",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.acknowledgedObjects"),
      options: {
        customBodyRenderLite: (dataIndex: number) =>
          `${innerData[dataIndex].acknowledgedObjects} / ${innerData[dataIndex].totalObjects}`,
        setCellHeaderProps: () => ({ style: { padding: "6px 16px" } }),
        setCellProps: () => ({ style: { padding: "6px 16px" } }),
      },
    },
  ];

  const innerData = [
    {
      objecttype: "View",
      fileDirectory: "/home/nikhil/tradex/schema/views/view.sql",
      totalObjects: 7,
      acknowledgedObjects: 2,
    },
    {
      objecttype: "Table",
      fileDirectory: "/home/nikhil/tradex/schema/tables/table.sql",
      totalObjects: 6,
      acknowledgedObjects: 0,
    },
  ];

  const rowCellComponent = (data: any, dataIndex: number) => {
    return (
      <Fragment key={`row-fragment-${data}`}>
        <TableRow>
          {data.map((val: any, index: number) => (
            <TableCell
              key={`row-${dataIndex}-body-cell-${index}`}
              className={clsx(
                classes.tableCell,
                expanded[dataIndex] && classes.rowTableCell,
                index === 0 && classes.w10
              )}
            >
              {typeof val === "function" ? val(dataIndex) : val}
            </TableCell>
          ))}
        </TableRow>
        {expanded[dataIndex] && (
          <TableRow>
            <TableCell colSpan={4} className={classes.innerTableParent}>
              <Box className={classes.innerTable}>
                <YBTable
                  data={innerData}
                  columns={innerColumns}
                  options={{
                    pagination: false,
                  }}
                />
              </Box>
            </TableCell>
          </TableRow>
        )}
      </Fragment>
    );
  };
  return rowCellComponent;
};

const ArrowComponent = (classes: ReturnType<typeof useStyles>, onClick: () => void) => () => {
  return (
    <Box className={classes.arrowComponent} onClick={onClick}>
      <ArrowRightIcon />
    </Box>
  );
};

export const MigrationAssessmentRefactoring: FC<MigrationAssessmentRefactoringProps> = ({
  sqlObjects,
  unsupportedDataTypes,
  unsupportedFeatures,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const featureOverview = useMemo(
    () =>
      unsupportedFeatures?.map((item) => ({
        objectCount: item.ObjectNames.length,
        feature: item.FeatureName,
      })) ?? [],
    [unsupportedFeatures]
  );

  const [selectedDataType, setSelectedDataType] = React.useState<RefactoringDataItems[number]>();
  const onSelectDataType = (dataIndex: number) => {
    setSelectedDataType(suggestionsErrors[dataIndex]);
  };

  const [expandedSuggestions, setExpandedSuggestions] = React.useState<boolean[]>([]);

  const refactoringOverviewColumns = [
    ...(ENABLE_MORE_DETAILS
      ? [
          {
            name: "",
            label: "",
            options: {
              sort: false,
              customBodyRenderLite: (dataIndex: number) => (
                <Box
                  px={1}
                  style={{ cursor: "pointer" }}
                  onClick={() => {
                    const newExpandedSuggestions = [...expandedSuggestions];
                    newExpandedSuggestions[dataIndex] = !expandedSuggestions[dataIndex];
                    setExpandedSuggestions(newExpandedSuggestions);
                  }}
                >
                  {expandedSuggestions[dataIndex] ? <MinusIcon /> : <PlusIcon />}
                </Box>
              ),
            },
          },
        ]
      : []),
    {
      name: "feature",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.unsupportedDataType"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "objectCount",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.objectCount"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    ...(ENABLE_MORE_DETAILS
      ? [
          {
            name: "",
            label: "",
            options: {
              sort: false,
              customBodyRenderLite: (dataIndex: number) =>
                ArrowComponent(classes, () => onSelectDataType(dataIndex))(),
              setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
            },
          },
        ]
      : []),
  ];

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

        <Box my={4}>
          <ResponsiveContainer width="100%" height={sqlObjects.length * 45}>
            <BarChart
              data={[...sqlObjects]}
              layout="vertical"
              margin={{
                right: 30,
                left: 30,
              }}
              barSize={16}
              barCategoryGap={34}
            >
              <CartesianGrid horizontal={false} strokeDasharray="3 3" />
              <XAxis type="number" />
              <YAxis type="category" dataKey="ObjectType" />
              <Tooltip content={<CustomTooltip />} />
              <Bar dataKey="TotalCount" fill="#2FB3FF" stackId="stack" isAnimationActive={false}>
                <LabelList dataKey="TotalCount" position="right" style={{ fill: "black" }} />
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
                      {payload.map((entry, index) => (
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
                          <div style={{ color: "#4E5F6D" }}>
                            {index === 0 ? "Automatic DDL Import" : formatter(entry.value)}
                          </div>
                        </li>
                      ))}
                    </ul>
                  );
                }}
              />
            </BarChart>
          </ResponsiveContainer>
        </Box>

        <Divider />

        <Box my={3}>
          <Typography variant="h5">
            {t("clusterDetail.voyager.planAndAssess.refactoring.conversionIssues")}
          </Typography>
        </Box>

        <Box position="relative">
          <YBTable
            data={featureOverview}
            columns={refactoringOverviewColumns}
            options={{
              customRowRender: ENABLE_MORE_DETAILS
                ? getRowCellComponent(featureOverview, expandedSuggestions, classes)
                : undefined,
              pagination: true,
            }}
            withBorder
          />

          {ENABLE_MORE_DETAILS && (
            <Box display="flex" justifyContent="end" position="absolute" right={10} top={6}>
              <YBButton
                variant="ghost"
                startIcon={
                  expandedSuggestions.filter((s) => s).length < featureOverview.length ? (
                    <ExpandIcon />
                  ) : (
                    <CollpaseIcon />
                  )
                }
                onClick={() => {
                  setExpandedSuggestions(
                    new Array(featureOverview.length).fill(
                      expandedSuggestions.filter((s) => s).length < featureOverview.length
                    )
                  );
                }}
              >
                {expandedSuggestions.filter((s) => s).length < featureOverview.length
                  ? t("clusterDetail.voyager.planAndAssess.refactoring.expandAll")
                  : t("clusterDetail.voyager.planAndAssess.refactoring.collapseAll")}
              </YBButton>
            </Box>
          )}
        </Box>
      </Box>

      <MigrationRefactoringSidePanel
        data={selectedDataType}
        onClose={() => setSelectedDataType(undefined)}
      />
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
        <Box color={payload[1]?.color ?? "#FFA400"}>
          Manual Refactoring: {payload[1]?.value ?? 0}
        </Box>
      </Box>
    );
  }

  return null;
};
