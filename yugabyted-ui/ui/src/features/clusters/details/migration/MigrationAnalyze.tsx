import React, { FC } from "react";
import {
  Accordion,
  AccordionDetails,
  AccordionSummary,
  Box,
  makeStyles,
  Paper,
  Typography,
  useTheme,
} from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "./MigrationOverview";
import { ErrorOutline, ExpandMore, Warning } from "@material-ui/icons";
import { Bar, BarChart, CartesianGrid, Legend, Tooltip, XAxis, YAxis } from "recharts";
import Editor, {  } from "@monaco-editor/react";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(5),
  },
  accordion: {
    borderRadius: theme.shape.borderRadius,
  },
  accordionSummary: {
    padding: theme.spacing(0, 2),
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    textTransform: "uppercase",
  },
}));

interface MigrationAnalyzeProps {
  heading: string;
  migration: Migration;
}

export const MigrationAnalyze: FC<MigrationAnalyzeProps> = ({ heading, migration }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const [value] = React.useState<string>("CREATE VIEW Failing_Students AS\nSELECT S_NAME, Student_ID\nFROM STUDENT\nWHERE GPA > 40;");

  const graphData = React.useMemo(
    () => [
      {
        xAxis: "SQL Objects",
        yAxis: "Count",
        table: 128,
        index: 77,
        function: 32,
      },
    ],
    []
  );

  return (
    <Paper>
      <Box p={4}>
        <Typography variant="h4" className={classes.heading}>
          {heading}
        </Typography>
        <Accordion className={classes.accordion} defaultExpanded>
          <AccordionSummary className={classes.accordionSummary} expandIcon={<ExpandMore />}>
            {t("clusterDetail.voyager.sqlEditor")}
          </AccordionSummary>
          <AccordionDetails>
            {/* <Input
              style={{ flex: 1, width: "100%", height: "80px" }}
              value={value}
              onChange={(e) => setValue(e.target.value)}
              minRows={3}
            /> */}
            <Editor height="200px" defaultLanguage="sql" defaultValue={value} options={{
              readOnly: true,
              minimap: {
                enabled: false,
              }
            }} />
          </AccordionDetails>
        </Accordion>

        <Accordion className={classes.accordion}>
          <AccordionSummary className={classes.accordionSummary} expandIcon={<ExpandMore />}>
            {t("clusterDetail.voyager.sqlObjects")}
          </AccordionSummary>
          <AccordionDetails>
            <BarChart
              width={400}
              height={300}
              data={graphData}
              margin={{
                top: 5,
                right: 30,
                left: 20,
                bottom: 5,
              }}
            >
              <CartesianGrid strokeDasharray="3 3" />
              <XAxis dataKey="xAxis" />
              <YAxis />
              <Tooltip />
              <Legend />
              <Bar dataKey="table" fill="#8884d8" />
              <Bar dataKey="index" fill="#029f00" />
              <Bar dataKey="function" fill="#af6f19" />
            </BarChart>
          </AccordionDetails>
        </Accordion>

        <Accordion className={classes.accordion}>
          <AccordionSummary className={classes.accordionSummary} expandIcon={<ExpandMore />}>
            <Box display="flex" alignItems="center" gridGap={theme.spacing(1)} flexGrow={1}>
              {t("clusterDetail.voyager.suggestionsErrors")}
              <Box
                borderRadius={theme.shape.borderRadius}
                bgcolor={theme.palette.warning[100]}
                px="5px"
                py="2px"
              >
                2
              </Box>
            </Box>
          </AccordionSummary>
          <AccordionDetails>
            <Box display="flex" gridGap={theme.spacing(1)} flexDirection="column">
              <Box display="flex" alignItems="center" gridGap={theme.spacing(1)}>
                <ErrorOutline color="error" />
                <Typography variant="body2">Invalid character at line 22, character 63</Typography>
              </Box>
              <Box display="flex" alignItems="center" gridGap={theme.spacing(1)}>
                <Warning />
                <Typography variant="body2">XYZ is deprecated, use ABC instead</Typography>
              </Box>
            </Box>
          </AccordionDetails>
        </Accordion>
      </Box>
    </Paper>
  );
};
