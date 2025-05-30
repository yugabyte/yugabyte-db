import React from "react";
import { Typography, Paper, Box, Link, LinearProgress, Divider } from "@material-ui/core";
import { makeStyles } from "@material-ui/core";
import { YBTable, YBButton } from "@app/components";
import { useQueryParams, StringParam } from "use-query-params";
import { useGetXclusterReplicationNamespacesInfoQuery } from "@app/api/src";
import CaretRightIcon from "@app/assets/cluster-right-icon.svg";
import AWSLogo from "@app/assets/logo-aws.svg";
import GCPLogo from "@app/assets/logo-gcp.svg";
import CloudLogo from "@app/assets/cloud-default-xcluster.svg"
import RefreshIcon from "@app/assets/refresh.svg";
import { useTranslation } from "react-i18next";
import { grey } from "@material-ui/core/colors";
import CheckSolidGreen from "@app/assets/check-solid-green.svg";
import { CircularProgress } from '@material-ui/core';
import { YBDropdown } from "@app/components";
import { MenuItem } from "@material-ui/core";
import TriangleDownIcon from '@app/assets/caret-down.svg';

const useStyles = makeStyles((theme) => ({
  container: {
    padding: theme.spacing(3),
    display: "flex",
    flexDirection: "column",
    gap: theme.spacing(4),
  },
  replicationIdBox: {
    padding: theme.spacing(2),
    backgroundColor: grey[400],
    borderRadius: theme.shape.borderRadius,
    boxShadow: theme.shadows[1],
  },
  clusterContainer: {
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    marginTop: theme.spacing(4),
    position: "relative",
    gap: theme.spacing(20)
  },
  clusterBox: {
    flex: 1,
    maxWidth: "20%",
    padding: theme.spacing(3),
    borderRadius: theme.shape.borderRadius,
    backgroundColor: "#eeeeee",
    textAlign: "left",
    border: "1px solid #ccc",
  },
  line: {
    position: "absolute",
    height: "1px",
    width: "357px",
    backgroundColor: "#757575",
    top: "50%",
    left: "50%",
    transform: "translate(-50%, -50%)",
    zIndex: 1,
  },
  arrowContainer: {
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
  },
  arrowIcon: {
    fontSize: "2rem",
  },
  loadingContainer: {
    marginTop: theme.spacing(4),
    textAlign: "center",
  },
  tableHeader: {
    fontWeight: "bold",
    textAlign: "center",
  },
  tableCell: {
    textAlign: "center",
    alignItems: "center"
  },
  logoZoneContainer: {
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    gap: theme.spacing(1),
    marginBottom: theme.spacing(1),
  },
  logo: {
    width: 50,
    height: 50,
  },
  zoneBox: {
    backgroundColor: "white",
    padding: theme.spacing(1),
    borderRadius: theme.shape.borderRadius,
    boxShadow: theme.shadows[1],
  }, errorContainer: {
    display: "flex",
    flexDirection: "column",
    alignItems: "center",
    justifyContent: "center",
    marginTop: theme.spacing(10),
    textAlign: "center",
  },
  errorLogo: {
    width: 80,
    height: 80,
    marginBottom: theme.spacing(2),
  },
  errorMessage: {
    marginBottom: theme.spacing(2),
    fontWeight: "bold",
    color: theme.palette.error.main,
  },
  errorLink: {
    color: theme.palette.primary.main,
    textDecoration: "none",
    "&:hover": {
      textDecoration: "underline",
    },
  },
  refreshButton: {
    position: "absolute",
    top: theme.spacing(35),
    right: theme.spacing(4),
  },
  throughputContainer: {
    width: "15%", // Slightly increased for better spacing
    margin: "-20px auto", // Centers the box with some margin
    textAlign: "center",
    padding: theme.spacing(3),
    backgroundColor: theme.palette.background.paper,
    borderRadius: theme.shape.borderRadius,
    boxShadow: "0px 4px 10px rgba(0, 0, 0, 0.1)", // Soft shadow for a modern look
    border: `1px solid ${theme.palette.divider}`, // Subtle border
    fontSize: "0.8rem"
  },
  throughputText: {
    display: "flex",
    justifyContent: "center",
    alignItems: "center",
    fontSize: "0.6rem",
    color: "#ffffff", // White text color for contrast
    padding: theme.spacing(1),
    backgroundColor: "#388e3c",
    borderRadius: theme.shape.borderRadius,
    boxShadow: "0px 2px 5px rgba(0, 0, 0, 0.1)",
  },
  uuidBox: {
    marginTop: theme.spacing(2),
    padding: theme.spacing(2),
    backgroundColor: "#ffffff",
    borderRadius: theme.shape.borderRadius,
    border: `1px solid ${theme.palette.divider}`,
    boxShadow: "0px 2px 5px rgba(0, 0, 0, 0.1)",
  },
  uuidLabel: {
    marginBottom: theme.spacing(1),
    fontWeight: 600,
    color: theme.palette.text.secondary,
  },

  uuidValue: {
    fontWeight: 700,
    color: theme.palette.text.primary,
    fontSize: "1.1rem",
  },
  replicatingText: {
    color: '#097345',
    backgroundColor: '#CDEFE1',
    padding: '1px',
    borderRadius: "10px",
    textAlign: 'center'
  },
  dropdownContent: {
    color: theme.palette.text.primary,
    fontWeight: 500,
    display: "flex",
    alignItems: "center",
    gap: theme.spacing(1),
  },
  dropdownHeader: {
    fontWeight: 600,
    color: theme.palette.grey[700],
    marginLeft: theme.spacing(2),
    marginRight: theme.spacing(2),
    fontSize: "12px",
    textTransform: "uppercase",
    paddingBottom: theme.spacing(1),
    borderBottom: `1px solid ${theme.palette.divider}`,
  },
  dropdownMenu: {
    backgroundColor: "#ffffff",
    borderRadius: theme.shape.borderRadius,
    boxShadow: "0px 6px 12px rgba(0, 0, 0, 0.15)",
    padding: theme.spacing(1),
    minWidth: "200px",
  },
  contentContainer: {
    display: "flex",
    flexDirection: "column",
    gap: theme.spacing(2),
    padding: theme.spacing(2),
  },

  namespaceWrapper: {
    display: "flex",
    flexDirection: "column",
    gap: theme.spacing(2),
    flexWrap: "wrap",
  },

  namespaceContainer: {
    display: "flex",
    alignItems: "center",
    width: "fit-content",
    backgroundColor: "#ffffff",
    padding: theme.spacing(1.5),
    borderRadius: theme.shape.borderRadius,
    boxShadow: "0px 4px 8px rgba(0, 0, 0, 0.1)",
    border: `1px solid ${theme.palette.divider}`,
  },
  namespaceLabel: {
    fontWeight: 500,
    fontSize: "14px",
    marginRight: theme.spacing(1),
  },
  dropdown: {
    cursor: "pointer",
    padding: theme.spacing(0.5, 1.5),
    borderRadius: theme.shape.borderRadius,
    backgroundColor: "#ffffff",
    boxShadow: "0px 4px 10px rgba(0, 0, 0, 0.15)",
    display: "flex",
    alignItems: "center",
    border: `1px solid ${theme.palette.divider}`,
  },
  tableContainer: {
    flexGrow: 1,
    padding: theme.spacing(2),
    borderRadius: theme.shape.borderRadius,
    boxShadow: "0px 4px 8px rgba(0, 0, 0, 0.1)",
  },
  noDataPaper: {
    padding: theme.spacing(2),
    textAlign: "center",
    backgroundColor: "#f9f9f9",
    borderRadius: theme.shape.borderRadius,
    border: `1px solid ${theme.palette.divider}`,
  },
}));

const XClusterNamespaceDetails: React.FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();
  const [{ replicationId }, setQueryParams] = useQueryParams({
    replicationId: StringParam,
    tab: StringParam,
  });

  const { data: namespaceData, isLoading, refetch: refetchXCluster } =
    useGetXclusterReplicationNamespacesInfoQuery({
      replication_id: replicationId ?? "",
    });
  const [isRefetching, setIsRefetching] = React.useState(false);
  const refetch = React.useCallback(() => {
    setIsRefetching(true);
    refetchXCluster().finally(() => setIsRefetching(false));
  }, [refetchXCluster]);

  const handleLinkClick = () => {
    setQueryParams({ replicationId: undefined });
  };

  React.useEffect(() => {
    if (!namespaceData?.replication_group_id || namespaceData?.replication_group_id.length === 0) {
      setQueryParams((prev) => {
        if (!prev.replicationId) return prev;
        const newParams = { ...prev };
        delete newParams.replicationId;
        return newParams;
      });
    }
  }, [namespaceData, setQueryParams]);


  const sourcePlacementLocation = namespaceData?.source_placement_location ?? [];
  const targetPlacementLocation = namespaceData?.target_placement_location ?? [];
  const [selectedNamespace, setSelectedNamespace] = React.useState<string>('');

  React.useEffect(() => {
    if (namespaceData?.namespace_list?.length) {
      setSelectedNamespace(namespaceData.namespace_list[0].namespace);
    }
  }, [namespaceData]);


  const selectedNamespaceData = namespaceData?.namespace_list?.find(
    (ns) => ns.namespace === selectedNamespace
  );

  const tableColumns: any = [
    {
      name: "table_name",
      label: t("clusterDetail.xcluster.tableName"),
      options: {
        setCellHeaderProps: () => ({ className: classes.tableHeader }),
        setCellProps: () => ({ className: classes.tableCell }),
      },
    },

    {
      name: "stream_id",
      label: t("clusterDetail.xcluster.streamID"),
      options: {
        setCellHeaderProps: () => ({ className: classes.tableHeader }),
        setCellProps: () => ({ className: classes.tableCell }),
      },
    },
    {
      name: "state",
      label: t("clusterDetail.xcluster.state"),
      options: {
        setCellHeaderProps: () => ({ className: classes.tableHeader }),
        setCellProps: () => ({ className: classes.tableCell }),
      },
    },
    {
      name: "avg_get_changes_latency_ms",
      label: t("Average get changes Latency (ms)"),
      options: {
        setCellHeaderProps: () => ({ className: classes.tableHeader }),
        setCellProps: () => ({ className: classes.tableCell }),
      },
    },
    {
      name: "avg_apply_latency_ms",
      label: t("Average apply latency (ms)"),
      options: {
        setCellHeaderProps: () => ({ className: classes.tableHeader }),
        setCellProps: () => ({ className: classes.tableCell }),
      },
    },
    {
      name: "avg_throughput_KiBps",
      label: t("Average throughput (ms)"),
      options: {
        setCellHeaderProps: () => ({ className: classes.tableHeader }),
        setCellProps: () => ({ className: classes.tableCell }),
      },
    }
  ];
  if (!isLoading && !namespaceData?.replication_group_id) {
    setQueryParams({ replicationId: undefined });
  }
  return (
    <Box className={classes.container}>
      <YBButton
        variant="ghost"
        startIcon={<RefreshIcon />}
        onClick={refetch}
        className={classes.refreshButton}
      >
        {t("clusterDetail.performance.actions.refresh")}
      </YBButton>
      <Box className={classes.replicationIdBox}>
        <Typography variant="body1">
          {replicationId && (
            <>
              <Link onClick={handleLinkClick} style={{ cursor: "pointer", color: "#0073e6" }}>
                {t("clusterDetail.xcluster.replicationList")}
              </Link>
              {` / ${replicationId}`}
            </>
          )}
        </Typography>
      </Box>
      <Box className={classes.clusterContainer}>
        <Box className={classes.clusterBox}>
          {(isLoading || isRefetching) ? <LinearProgress /> :
            <>
              <Typography variant="overline">
                {t('clusterDetail.xcluster.sourceCluster')}
              </Typography>
              <Divider style={{
                borderColor: '#d3d3d3',
                borderWidth: 1,
                borderStyle: 'solid',
                marginTop: 10
              }} />
              <Box mt={2}>
                {sourcePlacementLocation.map((location, index) => (
                  <Box key={index} mt={1}>
                    <Box className={classes.logoZoneContainer}>
                      {location.cloud?.toLowerCase() === "aws" ? (
                        <AWSLogo className={classes.logo} />
                      ) : location.cloud?.toLowerCase() === "gcp" ? (
                        <GCPLogo className={classes.logo} />
                      ) : (
                        <CloudLogo className={classes.logo} />
                      )}
                      <Box className={classes.zoneBox}>
                        <Typography variant="body2">{location.zone}</Typography>
                      </Box>
                    </Box>
                  </Box>
                ))}
              </Box>

              <Box className={classes.uuidBox}>
                <Typography variant="body2" className={classes.uuidLabel}>
                  {t("clusterDetail.xcluster.sourceUniverseUUID")}
                </Typography>
                <Typography variant="body2" className={classes.uuidValue}>
                  {namespaceData?.source_universe_uuid ??
                    t("clusterDetail.voyager.planAndAssess.complexity.notAvailable")}
                </Typography>
              </Box>
            </>}
        </Box>

        <div className={classes.line}></div>
        <Box className={classes.arrowContainer}>
          <CaretRightIcon className={classes.arrowIcon} />
        </Box>

        <Box className={classes.clusterBox}>
          {(isLoading || isRefetching) ? <LinearProgress /> :
            <>
              <Typography variant="overline">
                {t('clusterDetail.xcluster.targetCluster')}
              </Typography>
              <Divider style={{
                borderColor: '#d3d3d3',
                borderWidth: 1,
                borderStyle: 'solid', marginTop: 10
              }} />
              <Box mt={2}>
                {targetPlacementLocation.map((location, index) => (
                  <Box key={index} mt={1}>
                    <Box className={classes.logoZoneContainer}>
                      {location.cloud?.toLowerCase() === "aws" ? (
                        <AWSLogo className={classes.logo} />
                      ) : location.cloud?.toLowerCase() === "gcp" ? (
                        <GCPLogo className={classes.logo} />
                      ) : (
                        <CloudLogo className={classes.logo} />
                      )}
                      <Box className={classes.zoneBox}>
                        <Typography variant="body2">{location.zone}</Typography>
                      </Box>
                    </Box>
                  </Box>
                ))}
              </Box>

              <Box className={classes.uuidBox}>
                <Typography variant="body2" className={classes.uuidLabel}>
                  {t("clusterDetail.xcluster.targetUniverseUUID")}
                </Typography>
                <Typography variant="body2" className={classes.uuidValue}>
                  {
                    namespaceData?.target_universe_uuid ??
                    t("clusterDetail.voyager.planAndAssess.complexity.notAvailable")
                  }
                </Typography>
              </Box>
            </>}
        </Box>
      </Box>

      <Box className={classes.throughputContainer}>
        <Box display="flex" alignItems="center" justifyContent="center"
          className={classes.replicatingText}>
          <Typography variant="inherit">{t("Replicating")}</Typography>
          <CheckSolidGreen style={{ margin: "10px 10px" }} />
        </Box>

        <Box display="flex" justifyContent="center" alignItems="center" marginTop={2} gridGap={4}>
          {(isLoading || isRefetching) ? (
            <CircularProgress color="primary" />
          ) : (
            <Typography variant="body2">
              {namespaceData?.namespace_list?.length ?? 0} {t("common.databases")}
            </Typography>
          )}
          {" | "}
          {(isLoading || isRefetching) ? (
            <CircularProgress color="primary" />
          ) : (
            <Typography variant="body2">
              {(() => {
                const tables =
                  namespaceData?.namespace_list?.flatMap((ns) => ns.table_info_list || []) || [];
                const totalThroughput =
                  tables.reduce((sum, table) => sum + (table.avg_throughput_KiBps || 0), 0);
                const avgThroughput =
                  tables.length > 0 ? totalThroughput / tables.length : 0;
                return avgThroughput.toFixed(2);
              })()} {t("clusterDetail.xcluster.kibps")}
            </Typography>
          )}
        </Box>
      </Box>



      {(isLoading || isRefetching) ? (
        <Box className={classes.loadingContainer}>
          <LinearProgress />
        </Box>
      ) : (
        <Box className={classes.contentContainer}>
          {selectedNamespace && (
            <Box className={classes.namespaceWrapper}>
              <Box className={classes.namespaceContainer}>
                <Typography variant="h6" className={classes.namespaceLabel}>
                  {t("clusterDetail.xcluster.database")}: {" "}
                </Typography>
                <YBDropdown
                  origin={
                    <Box display="flex" alignItems="center" className={classes.dropdownContent}>
                      {selectedNamespace}
                      <TriangleDownIcon />
                    </Box>
                  }
                  position="bottom"
                  growDirection="right"
                  className={classes.dropdown}
                >
                  <Box className={classes.dropdownHeader}>
                    {t('clusterDetail.databases.database')}
                  </Box>
                  {namespaceData?.namespace_list?.map((namespace) => (
                    <MenuItem
                      key={namespace.namespace}
                      selected={namespace.namespace === selectedNamespace}
                      onClick={() => setSelectedNamespace(namespace.namespace)}
                    >
                      {namespace.namespace}
                    </MenuItem>
                  ))}
                </YBDropdown>
              </Box>

              {namespaceData?.namespace_list?.find(ns =>
                ns.namespace === selectedNamespace)?.table_info_list?.length ? (
                <Paper className={classes.tableContainer}>
                  <YBTable
                    data={selectedNamespaceData?.table_info_list ?? []}
                    columns={tableColumns}
                    options={{
                      pagination: (selectedNamespaceData?.table_info_list?.length ?? 0) >= 10
                    }}
                  />
                </Paper>
              ) : (
                <Paper className={classes.noDataPaper}>
                  <Typography>{t("clusterDetail.xcluster.noTablesInNamespace")}</Typography>
                </Paper>
              )}
            </Box>
          )}
        </Box>
      )}


    </Box>
  );
};

export default XClusterNamespaceDetails;
