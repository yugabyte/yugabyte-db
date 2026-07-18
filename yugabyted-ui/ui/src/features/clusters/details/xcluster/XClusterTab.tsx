import React, { useEffect, useState, FC } from "react";
import { useGetXclusterReplicationIDsQuery } from "@app/api/src";
import { LinearProgress, Paper, Typography, makeStyles, Box } from "@material-ui/core";
import { YBTable, YBLoadingBox, YBButton, GenericFailure, YBModal } from "@app/components";
import { useTranslation } from "react-i18next";
import ArrowRightIcon from "@app/assets/caret-right-circle.svg";
import { useQueryParams, StringParam } from 'use-query-params';
import XClusterNamespaceDetails from "./XClusterNamespaceDetails";
import type { XClusterOutboundGroup, TableReplicationLagDetailsOutbound } from "@app/api/src";
import type { XClusterInboundGroup } from "@app/api/src";
import RefreshIcon from "@app/assets/refresh.svg";


const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(3),
    fontWeight: "bold",
    color: theme.palette.text.primary,
    display: "flex",
    justifyContent: "space-between",
    alignItems: "center",
  },
  refreshButton: {
    marginLeft: theme.spacing(2),
  },
  tableContainer: {
    position: "relative",
    marginTop: theme.spacing(2),
    padding: theme.spacing(4),
    borderRadius: theme.shape.borderRadius,
    backgroundColor: "white",
  },
  tableHeader: {
    fontWeight: "bold",
    textAlign: "center",
    padding: theme.spacing(1.5, 2),
    width: "20px",
    height: "60px",
    backgroundColor: "white",
  },
  tableCell: {
    textAlign: "center",
    backgroundColor: "#f5f5f5",
    padding: theme.spacing(1.5, 2),
  },
  noDataPaper: {
    marginTop: theme.spacing(2),
    boxShadow: theme.shadows[2],
    borderRadius: theme.shape.borderRadius,
    textAlign: "center",
  },
  greyBox: {
    backgroundColor: "#f3f3f3",
    height: "150px",
    width: "100%",
    borderRadius: "8px",
    boxShadow: "0px 4px 6px rgba(0, 0, 0, 0.1)",
  },
  cellStyle: {
    padding: '9px 4px',
    textAlign: 'center',
    justifyContent: 'center',
  },
}));

const XClusterTab: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [selectedReplicationGroup,  setSelectedReplicationGroup] = useState<string>("");
  const [outboundSidePanelDataIndex, setOutboundSidePanelDataIndex] = useState<number | null>(null);
  const [{ replicationId }, setQueryParams] = useQueryParams({
    replicationId: StringParam,
    tab: StringParam,
  });

  // Redirecting from /?tab=xCluster to /?replicationId={replication_id}&tab=xCluster
  const handleArrowClickInbound = (replicationGroup: XClusterInboundGroup) => {
    setQueryParams({
      tab: "xCluster",
      replicationId: replicationGroup.replication_group_id,
    });
  };

  const handleArrowClickOutbound = (replicationGroup: string, dataIndex: number) => {
    setOutboundSidePanelDataIndex(dataIndex);
    setSelectedReplicationGroup(replicationGroup);
  }

  const { data: xClusterMetrics, isLoading, isError, refetch: refetchXCluster } =
    useGetXclusterReplicationIDsQuery({});

  const [isRefetching, setIsRefetching] = useState<boolean>(false);

  const refetch = React.useCallback(() => {
    setIsRefetching(true);
    refetchXCluster().finally(() => setIsRefetching(false));
  }, [refetchXCluster]);

  const [inboundReplicationGroups, setInboundReplicationGroups] =
    useState<XClusterInboundGroup[]>([]);
  const [outboundReplicationGroups, setOutboundReplicationGroups] =
    useState<XClusterOutboundGroup[]>([]);

  const [tableList, setTableList] = useState<(TableReplicationLagDetailsOutbound[] | null)[]>([]);

  useEffect(() => {
    if (xClusterMetrics?.inbound_replication_groups) {
      setInboundReplicationGroups(xClusterMetrics.inbound_replication_groups);
    }
    if (xClusterMetrics?.outbound_replication_groups) {
      setOutboundReplicationGroups(xClusterMetrics.outbound_replication_groups);
      setTableList(
        xClusterMetrics.outbound_replication_groups.map((group: XClusterOutboundGroup) =>
          group.tables_list_with_lag || []
        )
      );
    }
  }, [xClusterMetrics]);

  useEffect(() => {
    return () => {
      setQueryParams({ replicationId: undefined });
    };
  }, [setQueryParams]);


  const replicationColumnsOutbound = (replicationGroups: XClusterOutboundGroup[]) => {
    const isOutbound: boolean = true;
    const commonCellStyles = {
      width: "20px",
      height: "50px",
      padding: "8px 8px",
      backgroundColor: "#f5f5f5",
      textAlign: "center",
    };
    const commonHeaderStyles = {
      width: "20px",
      padding: "8px",
      height: "60px",
      backgroundColor: "white",
      textAlign: "center",
    };


    return [
      {
        name: "replication_group_id",
        label: t("clusterDetail.xcluster.replicationGroupId"),
        options: {
          setCellHeaderProps: () => ({
            className: classes.tableHeader,
            style: { ...commonHeaderStyles },
          }),
          setCellProps: () => ({
            className: classes.tableCell,
            style: { ...commonCellStyles },
          }),
          customBodyRenderLite: (dataIndex: number) => {
            return (
              <Box display="flex" alignItems="center" justifyContent="center" width="100%">
                <Box style={{ flex: 1 }}>
                  {replicationGroups[dataIndex].replication_group_id}
                </Box>
              </Box>
            );
          },
          display: isOutbound
        },
      },
      {
        name: "state",
        label: t("clusterDetail.xcluster.state"),
        options: {
          setCellHeaderProps: () => ({
            className: classes.tableHeader,
            style: { ...commonHeaderStyles },
          }),
          setCellProps: () => ({
            className: classes.tableCell,
            style: { ...commonCellStyles, width: "10px" },
          }),
          display: isOutbound
        },
      },
      {
        name: "source_cluster_node_ips",
        label: t("clusterDetail.xcluster.sourceClusterNodeIPS"),
        options: {
          setCellHeaderProps: () => ({
            className: classes.tableHeader,
            style: { ...commonHeaderStyles },
          }),
          setCellProps: () => ({
            className: classes.tableCell,
            style: { ...commonCellStyles },
          }),
          display: isOutbound,
          customBodyRenderLite: (dataIndex: number) => {
            const rowData: XClusterOutboundGroup = replicationGroups[dataIndex];
            const targetNodeIps: string[] =
              (rowData as XClusterOutboundGroup)?.replication_cluster_node_ips || [];
            return (
              <Box display="flex" flexDirection="column" minWidth="120px" maxHeight="150px"
                overflow="auto">
                {targetNodeIps.length > 0 ? (
                  targetNodeIps.map((ip: string) => (
                    <Box key={ip} padding="4px">
                      <Typography variant="body2">{ip}</Typography>
                    </Box>
                  ))
                ) : (
                  <Typography variant="body2">
                    {t("clusterDetail.voyager.planAndAssess.complexity.notAvailable")}
                  </Typography>
                )}
              </Box>
            );
          },
        },
      },
      {
        name: "source_universe_uuid",
        label: t("clusterDetail.xcluster.sourceUniverseUUID"),
        options: {
          setCellHeaderProps: () => ({
            className: classes.tableHeader,
            style: { ...commonHeaderStyles },
          }),
          setCellProps: () => ({
            className: classes.tableCell,
            style: { ...commonCellStyles },
          }),
          display: isOutbound,
          customBodyRenderLite: (dataIndex: number) => {
            const rowData = replicationGroups[dataIndex];

            return (
              <Box display="flex" alignItems="center" justifyContent="center" width="100%">
                <Typography variant="body2" style={{ textAlign: "center", flex: 1 }}>
                  {
                    rowData?.source_universe_uuid ??
                    t("clusterDetail.voyager.planAndAssess.complexity.notAvailable")
                  }
                </Typography>
              </Box>
            );
          },
        },
      },
      {
        name: "target_universe_uuid",
        label: t("clusterDetail.xcluster.targetUniverseUUID"),
        options: {
          setCellHeaderProps: () => ({
            className: classes.tableHeader,
            style: { ...commonHeaderStyles },
          }),
          setCellProps: () => ({
            className: classes.tableCell,
            style: { ...commonCellStyles },
          }),
          customBodyRenderLite: (dataIndex: number) => {
            const rowData: XClusterOutboundGroup = replicationGroups[dataIndex];

            return (
              <Box display="flex" alignItems="center" justifyContent="center" width="100%">
                <Typography variant="body2" style={{ textAlign: "center", flex: 1 }}>
                  {
                    rowData?.target_universe_uuid ??
                    t("clusterDetail.voyager.planAndAssess.complexity.notAvailable")
                  }
                </Typography>
                {isOutbound && rowData !== null && (
                  <ArrowRightIcon
                    onClick={() => {
                      handleArrowClickOutbound(rowData?.replication_group_id, dataIndex);
                    }}
                    style={{
                      cursor: "pointer",
                      marginLeft: "auto",
                    }}
                  />
                )}
              </Box>
            );
          },
        },
      },
    ]
  }


  const replicationColumnsInbound = (replicationGroups: XClusterInboundGroup[]) => {
    const isInbound: boolean = true;
    const commonCellStyles = {
      width: "20px",
      height: "50px",
      padding: "8px 8px",
      backgroundColor: "#f5f5f5",
      textAlign: "center" as const,
    };
    const commonHeaderStyles = {
      width: "20px",
      padding: "8px",
      height: "60px",
      backgroundColor: "white",
      textAlign: "center",
    };

    return [
      {
        name: "replication_group_id",
        label: t("clusterDetail.xcluster.replicationGroupId"),
        options: {
          setCellHeaderProps: () => ({
            className: classes.tableHeader,
            style: { ...commonHeaderStyles },
          }),
          setCellProps: () => ({
            className: classes.tableCell,
            style: { ...commonCellStyles },
          }),
          customBodyRenderLite: (dataIndex: number) => {
            return (
              <Box
                display="flex"
                alignItems="center"
                justifyContent="center"
                width="100%"
              >
                <Box
                  style={{
                    flex: 1,
                  }}
                >
                  {replicationGroups[dataIndex].replication_group_id}
                </Box>
              </Box>

            );
          },
          display: isInbound
        },
      },
      {
        name: "state",
        label: t("clusterDetail.xcluster.state"),
        options: {
          setCellHeaderProps: () => ({
            className: classes.tableHeader,
            style: { ...commonHeaderStyles },
          }),
          setCellProps: () => ({
            className: classes.tableCell,
            style: { ...commonCellStyles, width: "10px" },
          }),
          display: isInbound
        },
      },
      {
        name: "source_cluster_node_ips",
        label: t("clusterDetail.xcluster.sourceClusterNodeIPS"),
        options: {
          setCellHeaderProps: () => ({
            className: classes.tableHeader,
            style: { ...commonHeaderStyles },
          }),
          setCellProps: () => ({
            className: classes.tableCell,
            style: { ...commonCellStyles },
          }),
          display: isInbound,
          customBodyRenderLite: (dataIndex: number) => {

            const rowData = replicationGroups[dataIndex];
            const sourceNodeIps = (rowData as XClusterInboundGroup)?.source_cluster_node_ips || [];

            return (
              <Box display="flex" flexDirection="column" minWidth="120px" maxHeight="150px"
                overflow="auto">
                {sourceNodeIps.length > 0 ? (
                  sourceNodeIps.map((ip, index) => (
                    <Box key={index} padding="4px">
                      <Typography variant="body2">{ip}</Typography>
                    </Box>
                  ))
                ) : (
                  <Typography variant="body2">
                    {t("clusterDetail.voyager.planAndAssess.complexity.notAvailable")}
                  </Typography>
                )}
              </Box>
            );
          },
        },
      },
      {
        name: "target_cluster_node_ips",
        label: t("clusterDetail.xcluster.targetClusterNodeIPS"),
        options: {
          setCellHeaderProps: () => ({
            className: classes.tableHeader,
            style: { ...commonHeaderStyles },
          }),
          setCellProps: () => ({
            className: classes.tableCell,
            style: { ...commonCellStyles },
          }),
          display: isInbound,
          customBodyRenderLite: (dataIndex: number) => {
            const rowData = replicationGroups[dataIndex];
            const targetNodeIps = (rowData as XClusterInboundGroup)?.target_cluster_node_ips || [];
            return (
              <Box display="flex" flexDirection="column" minWidth="120px" maxHeight="150px"
                overflow="auto">
                {targetNodeIps.length > 0 ? (
                  targetNodeIps.map((ip: string, index: number) => (
                    <Box key={`targetNodeIps-${index}`} padding="4px">
                      <Typography variant="body2">{ip}</Typography>
                    </Box>
                  ))
                ) : (
                  <Typography variant="body2">
                    {t("clusterDetail.voyager.planAndAssess.complexity.notAvailable")}
                  </Typography>
                )}
              </Box>
            );
          },
        },
      },
      {
        name: "source_universe_uuid",
        label: t("clusterDetail.xcluster.sourceUniverseUUID"),
        options: {
          setCellHeaderProps: () => ({
            className: classes.tableHeader,
            style: { ...commonHeaderStyles },
          }),
          setCellProps: () => ({
            className: classes.tableCell,
            style: { ...commonCellStyles },
          }),
          customBodyRenderLite: (dataIndex: number) => {
            const rowData = replicationGroups[dataIndex];

            return (
              <Box display="flex" alignItems="center" justifyContent="center" width="100%">
                <Typography variant="body2" style={{ textAlign: "center", flex: 1 }}>
                  {
                    rowData?.source_universe_uuid ??
                    t("clusterDetail.voyager.planAndAssess.complexity.notAvailable")
                  }
                </Typography>
              </Box>
            );
          },
        },
      },
      {
        name: "target_universe_uuid",
        label: t("clusterDetail.xcluster.targetUniverseUUID"),
        options: {
          setCellHeaderProps: () => ({
            className: classes.tableHeader,
            style: { ...commonHeaderStyles },
          }),
          setCellProps: () => ({
            className: classes.tableCell,
            style: { ...commonCellStyles },
          }),
          customBodyRenderLite: (dataIndex: number) => {
            const rowData = replicationGroups[dataIndex];

            return (
              <Box display="flex" alignItems="center" justifyContent="center" width="100%">
                <Typography variant="body2" style={{ textAlign: "center", flex: 1 }}>
                  {
                    rowData?.target_universe_uuid ??
                    t("clusterDetail.voyager.planAndAssess.complexity.notAvailable")
                  }
                </Typography>
                {isInbound && (
                  <ArrowRightIcon
                    onClick={() => {
                      if (rowData) {
                        handleArrowClickInbound(rowData);
                      }
                    }}
                    style={{
                      cursor: "pointer",
                      marginLeft: "auto",
                    }}
                  />
                )}
              </Box>
            );
          },
        },
      },
    ];
  }

  const innerColumns = [
    {
      name: "table_uuid",
      label: t("clusterDetail.xcluster.tableUUID"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ className: classes.cellStyle }),
      setCellProps: () => ({ className: classes.cellStyle }),
      },
    },
    {
      name: "table_name",
      label: t("clusterDetail.xcluster.tableName"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ className: classes.cellStyle }),
        setCellProps: () => ({
          className: classes.cellStyle,
        }),
      },
    },
    {
      name: "namespace",
      label: t("clusterDetail.xcluster.database"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ className: classes.cellStyle }),
        setCellProps: () => ({
          className: classes.cellStyle,
        }),
      },
    },
    {
      name: "is_checkpointing",
      label: t("clusterDetail.xcluster.isCheckpointing"),
      options: {
        sort: false,
        customBodyRender: (value: boolean) => (value ? t('common.yes') : t('common.no')),
        setCellHeaderProps: () => ({ className: classes.cellStyle }),
        setCellProps: () => ({ className: classes.cellStyle }),
      },
    },
    {
      name: "is_part_of_initial_bootstrap",
      label: t("clusterDetail.xcluster.is_part_of_initial_bootstrap"),
      options: {
        sort: false,
        customBodyRender: (value: boolean) => (value ? t('common.yes') : t('common.no')),
        setCellHeaderProps: () => ({ className: classes.cellStyle }),
        setCellProps: () => ({ className: classes.cellStyle }),
      },
    },
    {
      name: "async_replication_committed_lag_micros",
      label: t("clusterDetail.xcluster.async_replication_committed_lag_micros"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ className: classes.cellStyle }),
        setCellProps: () => ({ className: classes.cellStyle }),
      },
    },
    {
      name: "async_replication_sent_lag_micros",
      label: t("clusterDetail.xcluster.async_replication_sent_lag_micros"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ className: classes.cellStyle }),
        setCellProps: () => ({ className: classes.cellStyle }),
      },
    },
  ];

  const renderTableOutbound = (outboundData: (XClusterOutboundGroup)[]) => {
    const columns = replicationColumnsOutbound(outboundData);
    return (
      <Paper className={classes.tableContainer}>
        {(isRefetching || isLoading) ? (
          <LinearProgress />
        ) : outboundData.length > 0 && (
          <>
            <YBTable
              data={outboundData}
              columns={columns}
              touchBorder
              cellBorder
              noCellBottomBorder={false}
              withBorder
              alternateRowShading
            />

            <YBModal
              open={selectedReplicationGroup.length > 0}
              title={t("clusterDetail.xcluster.tablesInvolvedInReplication")}
              onClose={() => setSelectedReplicationGroup("")}
              enableBackdropDismiss
              titleSeparator
              isSidePanel={true}
              size="lg"
            >
              <YBTable
                key={`innerTable-${outboundSidePanelDataIndex}`}
                data={tableList[outboundSidePanelDataIndex as number] || []}
                columns={innerColumns}
                options={{
                 pagination: ((tableList[outboundSidePanelDataIndex as number]?.length ?? 0) >= 10),
                }}
                cellBorder
                alternateRowShading
                withBorder={true}
                noCellBottomBorder
              />
            </YBModal>
          </>
        )}
      </Paper>
    );
  }


  const renderTableInbound = (
    data: (XClusterInboundGroup)[]
  ) => {
    const columns = replicationColumnsInbound(data);

    return (
      <Paper className={classes.tableContainer}>
        {(isRefetching || isLoading) ? (
          <LinearProgress />
        ) : data.length > 0 && (
          <>

            <YBTable
              data={data}
              columns={columns}
              touchBorder
              cellBorder
              noCellBottomBorder={false}
              withBorder
            />
          </>
        )}
      </Paper>
    );
  };
  return (
    <>
      <Typography variant="body1" className={classes.heading}>
        <span style={{ fontSize: "24px", fontWeight: "bold" }}>
          {t('clusterDetail.xcluster.xclusterReplications')}
        </span>
        {
          replicationId === undefined && (
            <YBButton
              variant="ghost"
              startIcon={<RefreshIcon />}
              onClick={refetch}
              className={classes.refreshButton}
            >
              {t('clusterDetail.charts.refresh')}
            </YBButton>
          )
        }
      </Typography>
      {replicationId ? (
        <Box>
          <XClusterNamespaceDetails />
        </Box>
      ) : (
        <>
          {isError && <GenericFailure />}
          {isLoading || isRefetching ? (
            <>
              <Box textAlign="center" pt={4} pb={4} width="100%">
                <LinearProgress />
              </Box>
              <Box className={classes.greyBox} />
            </>
          ) : (
            <>
              <Box className={classes.tableContainer}>
                <Typography variant="h6" className={classes.heading}>
                  {t("clusterDetail.xcluster.outboundReplications")}
                </Typography>
                {outboundReplicationGroups.length > 0 ?
                  renderTableOutbound(outboundReplicationGroups) : (
                  <Paper className={classes.noDataPaper}>
                    <YBLoadingBox>{t("clusterDetail.xcluster.noOutboundFound")}</YBLoadingBox>
                  </Paper>
                )}
              </Box>
              <Box className={classes.tableContainer}>
                <Typography variant="h6" className={classes.heading}>
                  {t("clusterDetail.xcluster.inboundReplications")}
                </Typography>
             {inboundReplicationGroups.length > 0 ? renderTableInbound(inboundReplicationGroups) : (
                  <Paper className={classes.noDataPaper}>
                    <YBLoadingBox>{t("clusterDetail.xcluster.noInboundFound")}</YBLoadingBox>
                  </Paper>
                )}
              </Box>
            </>
          )}
        </>
      )}
    </>
  );
};

export default XClusterTab;
