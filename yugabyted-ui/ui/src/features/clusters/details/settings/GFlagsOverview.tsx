import React, { FC, useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, LinearProgress, makeStyles, MenuItem, Paper, Typography } from '@material-ui/core';
import { YBSelect, YBTable } from '@app/components';
import { useGetClusterNodesQuery } from '@app/api/src';
import axios from 'axios';

const useStyles = makeStyles((theme) => ({
  paperContainer: {
    padding: theme.spacing(3),
    paddingBottom: theme.spacing(4),
    border: `1px solid ${theme.palette.grey[200]}`,
    width: '100%'
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
    minWidth: '180px',
  },
}));

type UpstreamFlagType = {
  name: string,
  value: string,
  type: string,
}

type FlagData = {
  flag: string,
  master?: string,
  tserver: string,
}

type GFlagData = {
  nodeInfoFlags: FlagData[],
  customFlags: FlagData[],
}

interface GFlagsOverviewProps {
  showDrift?: boolean,
  toggleDrift?: () => void,
}

export const GFlagsOverview: FC<GFlagsOverviewProps> = (/* { showDrift, toggleDrift } */) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const { data: nodesResponse, isFetching: isFetchingNodes } = useGetClusterNodesQuery();
  const nodesNamesList = nodesResponse?.data.map((node) => ({ label: node.name, value: node.host })) ?? [];

  const [isFetchingGFlags, setIsFetchingGFlags] = React.useState<boolean>(false);

  const [currentNode, setCurrentNode] = useState<string | undefined>('');
  React.useEffect(() => {
    if (!currentNode && nodesResponse) {
      setCurrentNode(nodesNamesList[0].value);
    }
  }, [nodesResponse])

  const [masterNodes, setMasterNodes] = React.useState<string[]>([]);
  React.useEffect(() => {
    if (!nodesResponse || !currentNode) {
      return;
    }

    const populateMasterNodes = async () => {
      setIsFetchingGFlags(true);

      const getMasterNodes = async (nodeName: string) => {
        try {
          const cpu = await axios.get(`http://${nodeName}:9000/api/v1/masters`)
            .then(({ data }) => data.master_server_and_type.map((l: any) => l.master_server.substring(0, l.master_server.indexOf(':'))))
            .catch(err => { console.error(err); return undefined; })
          return cpu;
        } catch (err) {
          console.error(err);
          return undefined;
        }
      }

      const masterNodes = await getMasterNodes(currentNode) as string[] | undefined;
      if (masterNodes) {
        setMasterNodes(masterNodes);
      }

      setIsFetchingGFlags(false);
    }

    populateMasterNodes();
  }, [nodesResponse, currentNode])

  const [gflagData, setGflagData] = React.useState<GFlagData>();
  useEffect(() => {
    if (!currentNode) {
      setGflagData(undefined);
      return;
    }

    const populateFlagData = async () => {
      const getFlags = async (nodeHost: string) => {
        try {
          const { data } = await axios.get<{ flags: UpstreamFlagType[] }>(`http://${nodeHost}/api/v1/varz`);
          const nodeInfoFlags = data.flags.filter(flag => flag.type === "NodeInfo")
          const customFlags = data.flags.filter(flag => flag.type === "Custom")
          return { nodeInfoFlags, customFlags };
        } catch (err) {
          console.error(err);
          return undefined;
        }
      }

      const masterFlags = masterNodes.includes(currentNode) ? await getFlags(`${currentNode}:7000`) : undefined;
      const tserverFlags = await getFlags(`${currentNode}:9000`);

      const nodeInfoFlagList = new Set<string>()
      masterFlags?.nodeInfoFlags.forEach(f => nodeInfoFlagList.add(f.name));
      tserverFlags?.nodeInfoFlags.forEach(f => nodeInfoFlagList.add(f.name));

      const customFlagList = new Set<string>()
      masterFlags?.customFlags.forEach(f => customFlagList.add(f.name));
      tserverFlags?.customFlags.forEach(f => customFlagList.add(f.name));

      setGflagData({
        nodeInfoFlags: Array.from(nodeInfoFlagList).map(flag => ({
          flag,
          ...(masterNodes.includes(currentNode) &&
            { master: masterFlags?.nodeInfoFlags.find(f => f.name === flag)?.value ?? '-' }
          ),
          tserver: tserverFlags?.nodeInfoFlags.find(f => f.name === flag)?.value ?? '-',
        })),
        customFlags: Array.from(customFlagList).map(flag => ({
          flag,
          ...(masterNodes.includes(currentNode) &&
            { master: masterFlags?.customFlags.find(f => f.name === flag)?.value ?? '-' }
          ),
          tserver: tserverFlags?.customFlags.find(f => f.name === flag)?.value ?? '-',
        }))
      });
    }

    populateFlagData();
  }, [masterNodes, currentNode])

  const gflagColumns = [
    {
      name: 'flag',
      label: t('clusterDetail.settings.gflags.flag'),
      options: {
        setCellProps: () => ({ style: { padding: '8px' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px' } }),
      }
    },
    {
      name: 'master',
      label: t('clusterDetail.settings.gflags.masterValue'),
      options: {
        setCellProps: () => ({ style: { padding: '8px', wordBreak: 'break-word' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px' } }),
      }
    },
    {
      name: 'tserver',
      label: t('clusterDetail.settings.gflags.tserverValue'),
      options: {
        setCellProps: () => ({ style: { padding: '8px', wordBreak: 'break-word' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px' } }),
      }
    },
  ];

  if (!currentNode || !masterNodes.includes(currentNode)) {
    gflagColumns.splice(1, 1);
  }

  return (
    <Paper className={classes.paperContainer}>
      <Box className={classes.heading}>
        <Typography variant="h4" >
          {t('clusterDetail.settings.gflags.title')}
        </Typography>
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
            {t('clusterDetail.settings.gflags.nodeInfoFlags')}
          </Typography>
          <YBTable
            data={gflagData?.nodeInfoFlags ?? []}
            columns={gflagColumns}
            options={{ pagination: false }}
            withBorder={false}
          />

          <Typography variant="h5" className={classes.subHeading}>
            {t('clusterDetail.settings.gflags.customFlags')}
          </Typography>
          <YBTable
            data={gflagData?.customFlags ?? []}
            columns={gflagColumns}
            options={{ pagination: false }}
            withBorder={false}
          />
        </>
      )}
    </Paper>
  );
};
