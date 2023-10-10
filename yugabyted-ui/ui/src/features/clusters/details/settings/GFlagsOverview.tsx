import React, { FC, useMemo, useState } from "react";
import { useTranslation } from "react-i18next";
import { Box, LinearProgress, makeStyles, MenuItem, Paper, Typography } from "@material-ui/core";
import { YBSelect, YBTable } from "@app/components";
import { useGetClusterNodesQuery, useGetGflagsQuery } from "@app/api/src";

const useStyles = makeStyles((theme) => ({
  paperContainer: {
    padding: theme.spacing(3),
    paddingBottom: theme.spacing(4),
    border: `1px solid ${theme.palette.grey[200]}`,
    width: "100%",
  },
  heading: {
    display: "flex",
    alignItems: "center",
    gap: theme.spacing(2),
  },
  subHeading: {
    marginTop: theme.spacing(5),
    marginBottom: theme.spacing(1.5),
  },
  selectBox: {
    marginTop: theme.spacing(-0.5),
    minWidth: "180px",
  },
}));

type UpstreamGflagResponseType = {
  master_flags: UpstreamFlagType[];
  tserver_flags: UpstreamFlagType[];
};

type UpstreamFlagType = {
  name: string;
  value: string;
  type: string;
};

interface GFlagsOverviewProps {
  showDrift?: boolean;
  toggleDrift?: () => void;
}

export const GFlagsOverview: FC<GFlagsOverviewProps> = (/* { showDrift, toggleDrift } */) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const { data: nodesResponse, isFetching: isFetchingNodes } = useGetClusterNodesQuery();
  const nodesNamesList =
    nodesResponse?.data.map((node) => ({ label: node.name, value: node.host })) ?? [];

  const [currentNode, setCurrentNode] = useState<string | undefined>("");
  React.useEffect(() => {
    if (!currentNode && nodesResponse) {
      setCurrentNode(nodesNamesList[0].value);
    }
  }, [nodesResponse]);

  const { data, isFetching: isFetchingGFlags } = useGetGflagsQuery<UpstreamGflagResponseType>(
    { node_address: currentNode || "" },
    { query: { enabled: !!currentNode } }
  );
  const gflagData = useMemo(() => {
    const masterFlags = {
      nodeInfoFlags: data?.master_flags?.filter((flag) => flag.type === "NodeInfo"),
      customFlags: data?.master_flags?.filter((flag) => flag.type === "Custom"),
    };
    const tserverFlags = {
      nodeInfoFlags: data?.tserver_flags?.filter((flag) => flag.type === "NodeInfo"),
      customFlags: data?.tserver_flags?.filter((flag) => flag.type === "Custom"),
    };

    const nodeInfoFlagList = new Set<string>();
    masterFlags?.nodeInfoFlags?.forEach((f) => nodeInfoFlagList.add(f.name));
    tserverFlags?.nodeInfoFlags?.forEach((f) => nodeInfoFlagList.add(f.name));

    const customFlagList = new Set<string>();
    masterFlags?.customFlags?.forEach((f) => customFlagList.add(f.name));
    tserverFlags?.customFlags?.forEach((f) => customFlagList.add(f.name));

    return {
      nodeInfoFlags: Array.from(nodeInfoFlagList).map((flag) => ({
        flag,
        master: masterFlags?.nodeInfoFlags?.find((f) => f.name === flag)?.value ?? "-",
        tserver: tserverFlags?.nodeInfoFlags?.find((f) => f.name === flag)?.value ?? "-",
      })),
      customFlags: Array.from(customFlagList).map((flag) => ({
        flag,
        master: masterFlags?.customFlags?.find((f) => f.name === flag)?.value ?? "-",
        tserver: tserverFlags?.customFlags?.find((f) => f.name === flag)?.value ?? "-",
      })),
    };
  }, [data]);

  const gflagColumns = [
    {
      name: "flag",
      label: t("clusterDetail.settings.gflags.flag"),
      options: {
        setCellProps: () => ({ style: { padding: "8px" } }),
        setCellHeaderProps: () => ({ style: { padding: "8px" } }),
      },
    },
    {
      name: "master",
      label: t("clusterDetail.settings.gflags.masterValue"),
      options: {
        setCellProps: () => ({ style: { padding: "8px", wordBreak: "break-word" } }),
        setCellHeaderProps: () => ({ style: { padding: "8px" } }),
      },
    },
    {
      name: "tserver",
      label: t("clusterDetail.settings.gflags.tserverValue"),
      options: {
        setCellProps: () => ({ style: { padding: "8px", wordBreak: "break-word" } }),
        setCellHeaderProps: () => ({ style: { padding: "8px" } }),
      },
    },
  ];

  return (
    <Paper className={classes.paperContainer}>
      <Box className={classes.heading}>
        <Typography variant="h4">{t("clusterDetail.settings.gflags.title")}</Typography>
        {!isFetchingNodes && (
          <YBSelect
            className={classes.selectBox}
            value={currentNode}
            onChange={(e) => setCurrentNode(e.target.value)}
          >
            {nodesNamesList?.map((el) => {
              return (
                <MenuItem key={el.label} value={el.value}>
                  {el.label}
                </MenuItem>
              );
            })}
          </YBSelect>
        )}
        {/* <YBButton onClick={toggleDrift}>
          {!showDrift ? t('clusterDetail.settings.gflags.showDrift') : t('clusterDetail.settings.gflags.hideDrift')}
        </YBButton> */}
      </Box>

      {isFetchingGFlags ||
        (isFetchingNodes && (
          <Box textAlign="center" pt={9} pb={9} width="100%">
            <LinearProgress />
          </Box>
        ))}

      {!isFetchingGFlags && !isFetchingNodes && (
        <>
          <Typography variant="h5" className={classes.subHeading}>
            {t("clusterDetail.settings.gflags.nodeInfoFlags")}
          </Typography>
          <YBTable
            data={gflagData.nodeInfoFlags}
            columns={gflagColumns}
            options={{ pagination: false }}
            withBorder={false}
          />

          <Typography variant="h5" className={classes.subHeading}>
            {t("clusterDetail.settings.gflags.customFlags")}
          </Typography>
          <YBTable
            data={gflagData.customFlags}
            columns={gflagColumns}
            options={{ pagination: false }}
            withBorder={false}
          />
        </>
      )}
    </Paper>
  );
};
