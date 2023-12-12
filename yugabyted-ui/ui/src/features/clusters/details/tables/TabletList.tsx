import React, { FC, useMemo } from "react";
import { makeStyles, Box, Typography, MenuItem, LinearProgress } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBButton, YBInput, YBLoadingBox, YBSelect, YBTable } from "@app/components";
import {
  useGetClusterHealthCheckQuery,
  useGetClusterNodesQuery,
  useGetTableInfoQuery,
} from "@app/api/src";
import SearchIcon from "@app/assets/search.svg";
import RefreshIcon from "@app/assets/refresh.svg";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";

const useStyles = makeStyles((theme) => ({
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: "uppercase",
    textAlign: "start",
  },
  value: {
    paddingTop: theme.spacing(0.36),
    fontWeight: theme.typography.fontWeightMedium as number,
    color: theme.palette.grey[800],
    fontSize: "18px",
    textAlign: "start",
  },
  arrowComponent: {
    textAlign: "end",
    "& svg": {
      marginTop: theme.spacing(0.25),
    },
  },
  statContainer: {
    display: "flex",
    gap: theme.spacing(4),
    marginTop: theme.spacing(3),
    marginBottom: theme.spacing(4),
  },
  selectBox: {
    minWidth: "200px",
    marginRight: theme.spacing(1),
    flexGrow: 0,
  },
  searchBox: {
    maxWidth: 520,
    flexGrow: 1,
    marginRight: "auto",
  },
  refreshBtn: {
    marginRight: theme.spacing(1),
  },
  checkbox: {
    padding: theme.spacing(0.5, 0.5, 0.5, 1.5),
  },
}));

type DatabaseListProps = {
  selectedTableUuid: string;
  onRefetch: () => void;
};

export const TabletList: FC<DatabaseListProps> = ({ selectedTableUuid, onRefetch }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const tableID = selectedTableUuid;

  const { data: nodesResponse, isFetching: isFetchingNodes } = useGetClusterNodesQuery({
    query: { refetchOnMount: "always" },
  });
  const hasReadReplica = !!nodesResponse?.data.find((node) => node.is_read_replica);
  const nodeNames = useMemo(() => nodesResponse?.data.map((node) => node.name), [nodesResponse]);

  const [selectedNode, setSelectedNode] = React.useState<string>("");
  const [tabletID, setTabletID] = React.useState<string>("");

  const { data: healthCheckData, isFetching: isFetchingHealth } = useGetClusterHealthCheckQuery({
    query: { refetchOnMount: "always" },
  });

  const {
    data: tableInfoData,
    isFetching: isFetchingTableInfo,
    refetch: refetchTableInfo,
  } = useGetTableInfoQuery(
    { id: tableID, node_address: selectedNode },
    { query: { enabled: !!tableID } }
  );

  const tabletList = React.useMemo(
    () =>
      tableInfoData?.tablets
        .map((tablet) => {
          // Determine leader, follower, and read replica nodes
          let leader = "";
          let followers: string[] = [];
          let read_replicas: string[] = [];
          tablet.raft_config?.forEach((node) => {
            try {
              let nodeAddress = node.location ? new URL(node.location).hostname : "";
              switch (node.role) {
                case "LEADER":
                  leader = nodeAddress;
                  break;
                case "FOLLOWER":
                  followers.push(nodeAddress);
                  break;
                case "READ_REPLICA":
                  read_replicas.push(nodeAddress);
                  break;
                default:
                  console.error("Unknown role " + node.role + " for node " + nodeAddress);
              }
            } catch {
                console.warn("Failed to get node address from given string: " + node.location +
                             ". This might be because the leader master node is down.");
            }
          });
          let hashPartition = tablet.partition
            ? [...tablet.partition?.matchAll(/hash_split: \[(.*)\]/g)][0][1]
            : "";
          return {
            id: tablet.tablet_id,
            range: hashPartition,
            leaderNode: leader,
            followerNodes: followers.join(", "),
            readReplicaNodes: read_replicas.join(", "),
            status:
                healthCheckData?.data?.under_replicated_tablets?.includes(tablet.tablet_id ?? "")
              ? "Under-replicated"
              : healthCheckData?.data?.leaderless_tablets?.includes(tablet.tablet_id ?? "")
              ? "Unavailable"
              : "",
          };
        })
        .filter(
          (tablet) =>
            selectedNode === "" ||
            tablet.leaderNode === selectedNode ||
            tablet.followerNodes.includes(selectedNode)
        ) ?? [],
    [tableInfoData, healthCheckData]
  );

  const filteredTabletList = useMemo(() => {
    let data = tabletList;
    if (tabletID) {
      const searchName = tabletID.toLowerCase();
      data = data.filter((data) => data.id?.toLowerCase().includes(searchName));
    }
    return data;
  }, [tabletList, tabletID]);

  const columns = useMemo(() => {
    const columns = [
      {
        name: "id",
        label: t("clusterDetail.databases.tabletID"),
        options: {
          setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
          setCellProps: () => ({ style: { padding: "8px 16px" } }),
        },
      },
      {
        name: "range",
        label: t("clusterDetail.databases.tabletRange"),
        options: {
          setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
          setCellProps: () => ({ style: { padding: "8px 16px" } }),
        },
      },
      {
        name: "leaderNode",
        label: t("clusterDetail.databases.leaderNode"),
        options: {
          setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
          setCellProps: () => ({ style: { padding: "8px 16px" } }),
        },
      },
      {
        name: "followerNodes",
        label: t("clusterDetail.databases.followerNodes"),
        options: {
          setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
          setCellProps: () => ({ style: { padding: "8px 16px" } }),
        },
      },
      {
        name: "readReplicaNodes",
        label: t("clusterDetail.databases.readReplicaNodes"),
        options: {
          setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
          setCellProps: () => ({ style: { padding: "8px 16px" } }),
        },
      },
      {
        name: "status",
        label: "",
        options: {
          sort: false,
          hideHeader: true,
          setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
          setCellProps: () => ({ style: { padding: "8px 16px" } }),
          customBodyRender: (status: string) =>
            status && (
              <YBBadge
                variant={status === "Under-replicated" ? BadgeVariant.Warning : BadgeVariant.Error}
                text={
                  status === "Under-replicated"
                    ? t("clusterDetail.databases.underReplicated")
                    : t("clusterDetail.databases.unavailable")
                }
              />
            ),
        },
      },
    ];

    if (nodesResponse && nodesResponse.data.length < 2) {
      columns.splice(
        columns.findIndex((col) => col.name === "followerNodes"),
        1
      );
    }

    if (!hasReadReplica) {
      columns.splice(
        columns.findIndex((col) => col.name === "readReplicaNodes"),
        1
      );
    }

    return columns;
  }, [nodesResponse, hasReadReplica]);

  if (isFetchingNodes || isFetchingHealth || isFetchingTableInfo) {
    return (
      <Box textAlign="center" mt={2.5}>
        <LinearProgress />
      </Box>
    );
  }

  return (
    <Box>
      <Box className={classes.statContainer}>
        <Box>
          <Typography variant="subtitle2" className={classes.label}>
            {t("clusterDetail.databases.totalTablets")}
          </Typography>
          <Typography variant="h4" className={classes.value}>
            {tabletList.length}
          </Typography>
        </Box>
      </Box>

      <Box display="flex" alignItems="center" justifyContent="space-between" mb={2}>
        <YBSelect
          className={classes.selectBox}
          value={selectedNode}
          onChange={(e) => {
            setSelectedNode(e.target.value);
          }}
        >
          <MenuItem value={""}>{t("clusterDetail.databases.allNodes")}</MenuItem>
          {nodeNames?.map((nodeName) => (
            <MenuItem key={nodeName} value={nodeName}>
              {nodeName}
            </MenuItem>
          ))}
        </YBSelect>

        <YBInput
          placeholder={t("clusterDetail.databases.searchTabletID")}
          InputProps={{
            startAdornment: <SearchIcon />,
          }}
          className={classes.searchBox}
          onChange={(ev) => setTabletID(ev.target.value)}
          value={tabletID}
        />

        <YBButton
          variant="ghost"
          startIcon={<RefreshIcon />}
          className={classes.refreshBtn}
          onClick={() => {
            refetchTableInfo();
            onRefetch();
          }}
        >
          {t("clusterDetail.performance.actions.refresh")}
        </YBButton>
      </Box>

      {!filteredTabletList.length ? (
        <YBLoadingBox>{t("clusterDetail.tables.noTabletsCopy")}</YBLoadingBox>
      ) : (
        <YBTable data={filteredTabletList} columns={columns} touchBorder={false} />
      )}
    </Box>
  );
};
