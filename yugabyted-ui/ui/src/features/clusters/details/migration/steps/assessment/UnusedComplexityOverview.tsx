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
  const complexityL = complexity.toLowerCase();

  const className =
    complexityL === "hard"
      ? classes.hardComp
      : complexityL === "medium"
      ? classes.mediumComp
      : complexityL === "easy"
      ? classes.easyComp
      : undefined;

  return <Box className={className}>{complexity || "N/A"}</Box>;
};

interface MigrationComplexityOverviewProps {
  heading: string;
  migration: Migration;
  onRefetch: () => void;
  isFetching?: boolean;
}

export const MigrationComplexityOverview: FC<MigrationComplexityOverviewProps> = ({
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
        schema: "YUGABYTED",
        sql_objects_count: 10,
        table_count: 2,
        complexity: "Easy",
      },
    ],
    []
  );

  const filteredData = React.useMemo(
    () => data.filter((item) => item.schema.toLowerCase().includes(searchQuery.toLowerCase())),
    [searchQuery, data]
  );

  const isFetchingAPI = false;
  const isErrorAPI = false;

  const complexityColumns = [
    {
      name: "schema",
      label: t("clusterDetail.voyager.planAndAssess.complexity.schema"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "sql_objects_count",
      label: t("clusterDetail.voyager.planAndAssess.complexity.sqlObjectCount"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "table_count",
      label: t("clusterDetail.voyager.planAndAssess.complexity.tableCount"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "complexity",
      label: t("clusterDetail.voyager.planAndAssess.complexity.complexity"),
      options: {
        customBodyRender: ComplexityComponent(classes),
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
              placeholder={t(
                "clusterDetail.voyager.planAndAssess.complexity.searchSchema"
              )}
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
            columns={complexityColumns}
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
