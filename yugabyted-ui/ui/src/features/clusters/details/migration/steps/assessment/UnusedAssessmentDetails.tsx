import React, { FC } from "react";
import { Box, LinearProgress, makeStyles } from "@material-ui/core";
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

interface MigrationAssessmentDetailsProps {
  heading: string;
  migration: Migration;
  onRefetch: () => void;
  isFetching?: boolean;
}

export const MigrationAssessmentDetails: FC<MigrationAssessmentDetailsProps> = ({
  /* heading,
  migration, */
  onRefetch,
  isFetching = false,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  /* const theme = useTheme(); */

  const [searchQuery, setSearchQuery] = React.useState<string>("");

  const data = React.useMemo(
    () => [
      {
        name: "Table_1",
        object_type: "Table",
        rowCount: 1000000,
        diskSize: "32 GB",
        readIops: "1000",
        writeIops: "1000",
      },
      {
        name: "Table_2",
        object_type: "Table",
        rowCount: 100000,
        diskSize: "16 GB",
        readIops: "1500",
        writeIops: "1000",
      },
      {
        name: "Index_1",
        object_type: "Index",
        rowCount: 200000,
        diskSize: "64 GB",
        readIops: "3000",
        writeIops: "3000",
      },
      {
        name: "Index_2",
        object_type: "Index",
        rowCount: 100000,
        diskSize: "16 GB",
        readIops: "1500",
        writeIops: "1000",
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

  const detailsColumns = [
    {
      name: "name",
      label: t("clusterDetail.voyager.planAndAssess.details.tableIndex"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "object_type",
      label: t("clusterDetail.voyager.planAndAssess.details.objectType"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "rowCount",
      label: t("clusterDetail.voyager.planAndAssess.details.rowCount"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "diskSize",
      label: t("clusterDetail.voyager.planAndAssess.details.diskSize"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "readIops",
      label: t("clusterDetail.voyager.planAndAssess.details.readIops"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "writeIops",
      label: t("clusterDetail.voyager.planAndAssess.details.writeIops"),
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
              placeholder={t("clusterDetail.voyager.planAndAssess.details.searchTableName")}
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
            columns={detailsColumns}
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
