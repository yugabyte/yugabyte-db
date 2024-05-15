import React, { FC, Fragment, useMemo } from "react";
import {
  Box,
  Divider,
  MenuItem,
  Paper,
  TableCell,
  TableRow,
  Typography,
  makeStyles,
} from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBCodeBlock, YBInput, YBSelect, YBTable, YBToggle } from "@app/components";
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
import SearchIcon from "@app/assets/search.svg";
import type { NameType, ValueType } from "recharts/types/component/DefaultTooltipContent";
import clsx from "clsx";

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
  tableRow: {
    cursor: "pointer",
    "&:hover": {
      backgroundColor: theme.palette.background.default,
    },
  },
  tableCell: {
    padding: theme.spacing(1, 2),
    maxWidth: 120,
    wordBreak: "break-word",
  },
  rowTableCell: {
    paddingTop: "10px",
    borderBottom: "unset",
  },
  actionsCell: {
    width: theme.spacing(8),
  },
  queryTableCell: {
    padding: theme.spacing(1),
  },
  queryCodeBlock: {
    lineHeight: 1.5,
    padding: theme.spacing(1),
  },
}));

interface MigrationAssessmentRefactoringProps {
  schemaList: readonly string[];
  sqlObjects: ReadonlyArray<{
    objectType: string;
    automaticDDLImport: number;
    manualRefactoring: number;
  }>;
  suggestionsErrors: ReadonlyArray<{
    objectType: string;
    filePath: string;
    reason: string;
    sql: string;
    issue: string;
  }>;
}

type SugErrAckArray = Array<
  MigrationAssessmentRefactoringProps["suggestionsErrors"][number] & { ack: boolean }
>;

const getRowCellComponent = (
  displayedRows: SugErrAckArray,
  classes: ReturnType<typeof useStyles>
) => {
  const rowCellComponent = (data: any, dataIndex: number) => {
    return (
      <Fragment key={`row-fragment-${data}`}>
        <TableRow className={classes.tableRow}>
          {data.map((val: any, index: number) => (
            <TableCell
              key={`row-${dataIndex}-body-cell-${index}`}
              className={clsx(classes.tableCell, classes.rowTableCell)}
            >
              {typeof val === "function" ? val(dataIndex) : val}
            </TableCell>
          ))}
        </TableRow>
        {!displayedRows[dataIndex].ack && (
          <TableRow>
            <TableCell colSpan={7} className={classes.queryTableCell}>
              <YBCodeBlock
                text={
                  <>
                    {displayedRows[dataIndex].sql}
                    <Divider className={classes.divider} />
                    <Typography variant="body2">{displayedRows[dataIndex].reason}</Typography>
                    <Divider className={classes.divider} />
                    <Typography variant="body2">{displayedRows[dataIndex].issue}</Typography>
                  </>
                }
                preClassName={classes.queryCodeBlock}
              />
            </TableCell>
          </TableRow>
        )}
      </Fragment>
    );
  };
  return rowCellComponent;
};

export const MigrationAssessmentRefactoring: FC<MigrationAssessmentRefactoringProps> = ({
  schemaList,
  sqlObjects,
  suggestionsErrors,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [selectedSchema, setSelectedSchema] = React.useState<string>("");
  const [search, setSearch] = React.useState<string>("");
  const [selectedObjectType, setSelectedObjectType] = React.useState<string>("");
  const [selectedAck, setSelectedAck] = React.useState<string>("");

  const [sugErrData, setSugErrData] = React.useState<SugErrAckArray>(
    suggestionsErrors.map((sugErr) => ({ ...sugErr, ack: false }))
  );

  const filteredSugErr = useMemo(() => {
    const searchQuery = search.toLowerCase();
    return sugErrData.filter(
      (sugErr) =>
        (selectedObjectType ? sugErr.objectType === selectedObjectType : true) &&
        (selectedAck ? sugErr.ack === (selectedAck === "Acknowledged") : true) &&
        (search
          ? sugErr.filePath.toLowerCase().includes(searchQuery) ||
            sugErr.reason.toLowerCase().includes(searchQuery)
          : true)
    );
  }, [search, selectedObjectType, selectedAck, sugErrData]);

  const suggestionErrorColumns = [
    {
      name: "objectType",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.objectType"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "filePath",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.fileDirectory"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "ack",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.acknowledge"),
      options: {
        sort: false,
        customBodyRenderLite: (dataIndex: number) => (
          <YBToggle
            checked={filteredSugErr[dataIndex].ack}
            label={t("clusterDetail.voyager.planAndAssess.refactoring.acknowledge")}
            onChange={(e) => {
              setSugErrData((prev) =>
                prev.map((sugErr, index) => ({
                  ...sugErr,
                  ack: index === dataIndex ? e.target.checked : sugErr.ack,
                }))
              );
            }}
          />
        ),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
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
          <Typography variant="h4">
            {t("clusterDetail.voyager.planAndAssess.refactoring.heading")}
          </Typography>
        </Box>
        <Box>
          <Typography variant="body1" className={classes.label}>
            {t("clusterDetail.voyager.planAndAssess.refactoring.schema")}
          </Typography>
          <YBSelect
            style={{ minWidth: "250px" }}
            value={selectedSchema}
            onChange={(e) => setSelectedSchema(e.target.value)}
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

        <Divider orientation="horizontal" variant="middle" className={classes.divider} />

        <Box display="flex" alignItems="center" gridGap={10} my={2}>
          <Box flex={3}>
            <Typography variant="body1" className={classes.label}>
              {t("clusterDetail.voyager.planAndAssess.refactoring.search")}
            </Typography>
            <YBInput
              className={classes.fullWidth}
              placeholder={t("clusterDetail.voyager.planAndAssess.refactoring.searchPlaceholder")}
              InputProps={{
                startAdornment: <SearchIcon />,
              }}
              onChange={(ev) => setSearch(ev.target.value)}
              value={search}
            />
          </Box>
          <Box flex={1}>
            <Typography variant="body1" className={classes.label}>
              {t("clusterDetail.voyager.planAndAssess.refactoring.objectType")}
            </Typography>
            <YBSelect
              className={classes.fullWidth}
              value={selectedObjectType}
              onChange={(e) => setSelectedObjectType(e.target.value)}
            >
              <MenuItem value="">All</MenuItem>
              <Divider className={classes.divider} />
              {sqlObjects.map((object) => {
                return (
                  <MenuItem key={object.objectType} value={object.objectType}>
                    {object.objectType}
                  </MenuItem>
                );
              })}
            </YBSelect>
          </Box>
          <Box flex={1}>
            <Typography variant="body1" className={classes.label}>
              {t("clusterDetail.voyager.planAndAssess.refactoring.acknowledged")}
            </Typography>
            <YBSelect
              className={classes.fullWidth}
              value={selectedAck}
              onChange={(e) => setSelectedAck(e.target.value)}
            >
              <MenuItem value="">All</MenuItem>
              <Divider className={classes.divider} />
              <MenuItem value="Acknowledged">Acknowledged</MenuItem>
              <MenuItem value="Not acknowledged">Not acknowledged</MenuItem>
            </YBSelect>
          </Box>
        </Box>

        <Box>
          <YBTable
            data={filteredSugErr}
            columns={suggestionErrorColumns}
            options={{
              customRowRender: getRowCellComponent(filteredSugErr, classes),
              pagination: true,
            }}
            withBorder={false}
          />
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
