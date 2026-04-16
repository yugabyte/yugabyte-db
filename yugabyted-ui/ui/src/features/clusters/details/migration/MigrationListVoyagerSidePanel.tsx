import React, { FC } from "react";
import { Box, Divider, Grid, Typography, makeStyles } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBModal } from "@app/components";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(4),
  },
  label: {
    fontSize: '11.5px',
    fontWeight: 500,
    color: '#6D7C88',
    textAlign: "left",
  },
  value: {
    paddingTop: theme.spacing(0.36),
    textAlign: "start",
  },
  dividerGridItem: {
    padding: `${theme.spacing(1)}px ${theme.spacing(2)}px !important`,
  },
}));

export interface VoyagerInstanceProps {
  machine_ip?: string;
  os?: string;
  avail_disk_bytes?: string;
  export_dir?: string;
  exported_schema_location?: string;
}

interface MigrationListVoyagerSidePanel extends VoyagerInstanceProps {
  open: boolean;
  onClose: () => void;
}

export const MigrationListVoyagerSidePanel: FC<MigrationListVoyagerSidePanel> = ({
  open,
  onClose,
  machine_ip,
  os,
  avail_disk_bytes,
  export_dir,
  exported_schema_location,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <YBModal
      open={open}
      title={t("clusterDetail.voyager.voyagerSidePanel.voyagerInstance")}
      onClose={onClose}
      enableBackdropDismiss
      titleSeparator
      cancelLabel={t("common.close")}
      isSidePanel
    >
      <Box my={2}>
        <Grid container spacing={4}>
          <Grid item xs={6}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.voyagerSidePanel.machineIP")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {machine_ip}
            </Typography>
          </Grid>
          <Grid item xs={6}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.voyagerSidePanel.os")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {os}
            </Typography>
          </Grid>
          <Grid item xs={12} className={classes.dividerGridItem}>
            <Divider orientation="horizontal" />
          </Grid>
          <Grid item xs={6}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.voyagerSidePanel.availDiskSpace")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {avail_disk_bytes}
            </Typography>
          </Grid>
          <Grid item xs={12} className={classes.dividerGridItem}>
            <Divider orientation="horizontal" />
          </Grid>
          <Grid item xs={12}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.voyagerSidePanel.exportDirectory")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {export_dir}
            </Typography>
          </Grid>
          <Grid item xs={12}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.voyagerSidePanel.exportedSchemaLocation")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {exported_schema_location}
            </Typography>
          </Grid>
        </Grid>
      </Box>
    </YBModal>
  );
};
