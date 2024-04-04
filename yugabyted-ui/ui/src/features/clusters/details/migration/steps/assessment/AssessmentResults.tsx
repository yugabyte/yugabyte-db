import React, { FC } from "react";
import { Box, LinearProgress, makeStyles, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "../../MigrationOverview";
import { GenericFailure, YBButton, YBInput, YBTable } from "@app/components";
import SearchIcon from "@app/assets/search.svg";
import RefreshIcon from "@app/assets/refresh.svg";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(4),
  },
  searchBox: {
    maxWidth: "360px",
  },
}));

interface MigrationAssessmentResultsProps {
  heading: string;
  migration: Migration;
  onRefetch: () => void;
  isFetching?: boolean;
}

export const MigrationAssessmentResults: FC<MigrationAssessmentResultsProps> = ({
  heading,
  migration,
  onRefetch,
  isFetching = false,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const [searchQuery, setSearchQuery] = React.useState<string>("");

  const data = React.useMemo(
    () => [
      {
        name: "Table_1",
        object_type: "Table",
        decisionCheck: "Colocated max size bytes",
        colocated: "Yes",
        splitPoints: "-",
      },
      {
        name: "Table_2",
        object_type: "Table",
        decisionCheck: "Colocated max IOPS",
        colocated: "No",
        splitPoints: "-",
      },
      {
        name: "Index_1",
        object_type: "Index",
        decisionCheck: "Colocated max IOPS",
        colocated: "No",
        splitPoints: "-",
      },
      {
        name: "Index_2",
        object_type: "Index",
        decisionCheck: "Colocated max IOPS",
        colocated: "No",
        splitPoints: "-",
      },
    ],
    []
  );

  const filteredData = React.useMemo(
    () => data.filter((item) => item.name.toLowerCase().includes(searchQuery.toLowerCase())),
    [searchQuery, data]
  );

  const isFetchingAPI = false;
  const isErrorAPI = false;

  const resultsColumns = [
    {
      name: "name",
      label: t("clusterDetail.voyager.planAndAssess.results.tableIndex"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "object_type",
      label: t("clusterDetail.voyager.planAndAssess.results.objectType"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "decisionCheck",
      label: t("clusterDetail.voyager.planAndAssess.results.decisionCheck"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "colocated",
      label: t("clusterDetail.voyager.planAndAssess.results.colocated"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "splitPoints",
      label: t("clusterDetail.voyager.planAndAssess.results.splitPoints"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
  ];

  return (
    <Box>
      {isErrorAPI && <GenericFailure />}

      {(isFetching || isFetchingAPI) && (
        <Box textAlign="center" pt={2} pb={2} width="100%">
          <LinearProgress />
        </Box>
      )}

      {!(isFetching || isFetchingAPI || isErrorAPI) && (
        <Box display="flex" flexDirection="column" gridGap={20} flex={1} py={2}>
          <Box display="flex" justifyContent="space-between" alignItems="start">
            <YBInput
              placeholder={t("clusterDetail.voyager.planAndAssess.results.searchTableName")}
              InputProps={{
                startAdornment: <SearchIcon />,
              }}
              className={classes.searchBox}
              onChange={(ev) => setSearchQuery(ev.target.value)}
              value={searchQuery}
            />
            <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={onRefetch}>
              {t("clusterDetail.performance.actions.refresh")}
            </YBButton>
          </Box>
          <YBTable
            data={filteredData}
            columns={resultsColumns}
            options={{
              pagination: true,
            }}
            withBorder={false}
          />
        </Box>
      )}
    </Box>
  );
};
