import React, { FC } from "react";
import { Box, LinearProgress, Link, makeStyles, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "../../MigrationOverview";
import { MigrationAssessmentReport, useGetMigrationAssessmentInfoQuery } from "@app/api/src";
import { MigrationAssessmentSummary } from "./AssessmentSummary";
import { MigrationSourceEnv } from "./AssessmentSourceEnv";
import { MigrationAssessmentRecommendation } from "./AssessmentRecommendation";
import { GenericFailure, YBButton, YBCodeBlock } from "@app/components";
import RefreshIcon from "@app/assets/refresh.svg";
import BookIcon from "@app/assets/book.svg";
import { StepCard } from "../schema/StepCard";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
interface MigrationAssessmentProps {
  heading: string;
  migration: Migration | undefined;
  step: number;
  onRefetch: () => void;
  isFetching?: boolean;
  isNewMigration?: boolean;
  operatingSystem?: string;
}

const useStyles = makeStyles((theme) => ({
  commandCodeBlock: {
    lineHeight: 1.5,
    padding: theme.spacing(1.2),
  },
  docsLink: {
    display: "flex",
    paddingRight: theme.spacing(1.5),
  },
  menuIcon: {
    marginRight: theme.spacing(1),
  },
}));

export const MigrationAssessment: FC<MigrationAssessmentProps> = ({
  migration,
  onRefetch,
  isFetching = false,
  isNewMigration = false,
  operatingSystem
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();
  const INSTALL_VOYAGER_DOCS_URL =
      "https://docs.yugabyte.com/preview/yugabyte-voyager/install-yb-voyager/"
  const ASSESS_MIGRATION_DOCS_URL =
      "https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/assess-migration/"
  // TODO: get correct linux install commands, and a way to distinguish different linux distros.
  const INSTALL_VOYAGER_OS_CMD: { [key: string]: string; } = {
    "rhel":  "sudo yum update\n" +
             "sudo yum install " +
             "https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/repos/reporpms/" +
             "yb-yum-repo-1.1-0.noarch.rpm\n" +
             "sudo yum install https://dl.fedoraproject.org/pub/epel/" +
             "epel-release-latest-8.noarch.rpm\n" +
             "sudo yum install oracle-instant-clients-repo\n" +
             "sudo yum --disablerepo=* -y install https://download.postgresql.org/pub/repos/" +
             "yum/reporpms/EL-8-x86_64/pgdg-redhat-repo-latest.noarch.rpm\n" +
             "sudo dnf -qy module disable postgresql\n" +
             "sudo yum install perl-open.noarch\n" +
             "sudo yum update\n" +
             "sudo yum install yb-voyager\n" +
             "yb-voyager version",
    "darwin": "brew tap yugabyte/tap\n" +
              "brew install yb-voyager\n" +
              "yb-voyager version",
    "ubuntu": "wget https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/repos/reporpms/" +
              "yb-apt-repo_1.0.0_all.deb\n" +
              "sudo apt-get install ./yb-apt-repo_1.0.0_all.deb\n" +
              "sudo apt install -y postgresql-common\n" +
              "sudo /usr/share/postgresql-common/pgdg/apt.postgresql.org.sh\n" +
              "sudo apt-get clean\n" +
              "sudo apt-get update\n" +
              "sudo apt-get install yb-voyager\n" +
              "yb-voyager version",
    "docker": "docker pull yugabytedb/yb-voyager\n" +
              "wget -O ./yb-voyager https://raw.githubusercontent.com/yugabyte/" +
              "yb-voyager/main/docker/yb-voyager-docker " +
              "&& chmod +x ./yb-voyager && sudo mv yb-voyager /usr/local/bin/yb-voyager\n" +
              "yb-voyager version",
    "git": "git clone https://github.com/yugabyte/yb-voyager.git\n" +
           "cd yb-voyager/installer_scripts\n" +
           "./install-yb-voyager"
  }

  const {
    data: newMigrationAPIData,
    isFetching: isFetchingAPI,
    isError: isErrorMigrationAssessmentInfo,
  } = useGetMigrationAssessmentInfoQuery({
    uuid: migration?.migration_uuid || "migration_uuid_not_found",
  });

  const RHELDistrosList: string[] = ["centos", "almalinux", "rhel"];

  const newMigrationAPI = newMigrationAPIData as MigrationAssessmentReport | undefined;

  const determineOSCommand = (os: string | undefined) => {
    if (!os) return INSTALL_VOYAGER_OS_CMD.git;
    if (RHELDistrosList.includes(os)) return INSTALL_VOYAGER_OS_CMD.rhel;
    return INSTALL_VOYAGER_OS_CMD[os.toLowerCase()] || INSTALL_VOYAGER_OS_CMD.git;
  };

  return (
    <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
      <Box display="flex" justifyContent="space-between" alignItems="center" mb={1}>
        <Typography variant="h4">{t("clusterDetail.voyager.planAndAssess.heading")}</Typography>
        <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={onRefetch}>
          {t("clusterDetail.performance.actions.refresh")}
        </YBButton>
        {/* {newMigrationAPI?.completedTime && (
          <Typography variant="body1" className={classes.label}>
            {newMigrationAPI.completedTime}
          </Typography>
        )} */}
      </Box>

      {isErrorMigrationAssessmentInfo && !isNewMigration && <GenericFailure />}

      {(isFetching || isFetchingAPI) && !isNewMigration && (
        <Box textAlign="center" pt={2} pb={2} width="100%">
          <LinearProgress />
        </Box>
      )}

      {!(isFetching || isFetchingAPI || isErrorMigrationAssessmentInfo) && !isNewMigration &&
          newMigrationAPI?.assessment_status && (
        <>
          <Box display="flex" flexDirection="row" gridGap={theme.spacing(2)} alignItems="stretch">
            <Box flexBasis="100%">
              <MigrationAssessmentSummary
                complexity={
                  newMigrationAPI?.summary?.migration_complexity || migration?.complexity || "N/A"
                }
                estimatedMigrationTime={newMigrationAPI?.summary?.estimated_migration_time || "N/A"}
              />
            </Box>
            <Box flexBasis="100%">
              <MigrationSourceEnv
                migration={migration}
              />
            </Box>
          </Box>
          <MigrationAssessmentRecommendation
            migration={migration}
            nodeCount={
              newMigrationAPI?.target_recommendations?.target_cluster_recommendation?.num_nodes ??
              "N/A"
            }
            vCpuPerNode={
              newMigrationAPI?.target_recommendations?.target_cluster_recommendation
                ?.vcpu_per_node ?? "N/A"
            }
            memoryPerNode={
              newMigrationAPI?.target_recommendations?.target_cluster_recommendation
                ?.memory_per_node ?? "N/A"
            }
            optimalSelectConnPerNode={
              newMigrationAPI?.target_recommendations?.target_cluster_recommendation
                ?.connections_per_node ?? "N/A"
            }
            optimalInsertConnPerNode={
              newMigrationAPI?.target_recommendations?.target_cluster_recommendation
                ?.inserts_per_node ?? "N/A"
            }
            colocatedTableCount={
              newMigrationAPI?.target_recommendations?.target_schema_recommendation
                ?.no_of_colocated_tables ?? "N/A"
            }
            shardedTableCount={
              newMigrationAPI?.target_recommendations?.target_schema_recommendation
                ?.no_of_sharded_tables ?? "N/A"
            }
            colocatedTotalSize={
              newMigrationAPI?.target_recommendations?.target_schema_recommendation
                ?.total_size_colocated_tables ?? "N/A"
            }
            shardedTotalSize={
              newMigrationAPI?.target_recommendations?.target_schema_recommendation
                ?.total_size_sharded_tables ?? "N/A"
            }
            sqlObjects={newMigrationAPI?.recommended_refactoring?.refactor_details}
            unsupportedDataTypes={newMigrationAPI?.unsupported_data_types}
            unsupportedFeatures={newMigrationAPI?.unsupported_features}
            unsupportedFunctions={newMigrationAPI?.unsupported_functions}
          />
        </>
      )}

      {!isFetching && !isFetchingAPI &&
        migration?.migration_phase === 1 && migration?.status === "IN PROGRESS" && (
          <StepCard
            title={t("clusterDetail.voyager.planAndAssess.assessMigration")}
            isLoading
            showInProgress
          />
      )}

      {(isNewMigration ||
        (!isFetching && !isFetchingAPI && !newMigrationAPI?.assessment_status)) &&
        !(migration?.migration_phase === 1 && migration?.status === "IN PROGRESS") && (<>
        <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
          <StepCard
            title={t("clusterDetail.voyager.planAndAssess.installVoyager")}
          >
            {() => {
              return <>
                <Box mt={2} mb={2} width="fit-content">
                  <Typography variant="body2">
                    {t("clusterDetail.voyager.planAndAssess.installVoyagerDesc")}
                  </Typography>
                </Box>
                {/* Ubuntu 22.* uses Ubuntu steps and all other versions will use git steps*/}
                <YBCodeBlock
                  text={determineOSCommand(operatingSystem === "UbuntuGeneric" ? "git"
                                            : operatingSystem)}
                  showCopyIconButton={true}
                  preClassName={classes.commandCodeBlock}
                />
                {operatingSystem && (RHELDistrosList.includes(operatingSystem) ||
                                        operatingSystem === "darwin") && (
                    <Box mt={2} mb={2}>
                        <YBBadge
                          variant={BadgeVariant.Info}
                          text={t("clusterDetail.voyager.planAndAssess.specificVersion")}
                          icon={true}
                        />
                        <Box mt={2} mb={2}>
                            <YBCodeBlock
                                text="sudo yum install yb-voyager-<VERSION>"
                                showCopyIconButton={false}
                                preClassName={classes.commandCodeBlock}
                            />
                        </Box>
                    </Box>
                )}
                <Box mt={2} mb={2} width="fit-content">
                  <Link
                    className={classes.docsLink}
                    href={INSTALL_VOYAGER_DOCS_URL}
                    target="_blank"
                  >
                    <BookIcon className={classes.menuIcon} />
                    <Typography
                      variant="body2"
                    >
                      {t("clusterDetail.voyager.planAndAssess.installVoyagerLearn")}
                    </Typography>
                  </Link>
                </Box>
              </>
            }}
          </StepCard>
          <StepCard
            title={t("clusterDetail.voyager.planAndAssess.assessMigration")}
          >
            {() => {
              return <>
                <Box mt={2} mb={2} width="fit-content">
                  <Typography variant="body2">
                    {t("clusterDetail.voyager.planAndAssess.assessMigrationDesc")}
                  </Typography>
                </Box>
                <YBCodeBlock
                  text={
                    "# Replace the argument values with those applicable " +
                    "for your migration\n" +
                    "yb-voyager assess-migration\n" +
                    "--source-db-type <SOURCE_DB_TYPE> \\\n" +
                    "--source-db-host <SOURCE_DB_HOST> \\\n" +
                    "--source-db-user <SOURCE_DB_USER> \\\n" +
                    "--source-db-password <SOURCE_DB_PASSWORD> \\\n" +
                    "--source-db-name <SOURCE_DB_NAME> \\\n" +
                    "--source-db-schema <SOURCE_DB_SCHEMA1>,<SOURCE_DB_SCHEMA2> \\\n" +
                    "--export-dir <EXPORT/DIR/PATH> \\\n" +
                    "--start-clean=true"
                  }
                  showCopyIconButton={true}
                  preClassName={classes.commandCodeBlock}
                  highlightSyntax={true}
                />
                <Box mt={2} mb={2} width="fit-content">
                  <Link
                    className={classes.docsLink}
                    href={ASSESS_MIGRATION_DOCS_URL}
                    target="_blank"
                  >
                    <BookIcon className={classes.menuIcon} />
                    <Typography
                      variant="body2"
                    >
                      {t("clusterDetail.voyager.planAndAssess.assessMigrationLearn")}
                    </Typography>
                  </Link>
                </Box>
              </>
            }}
          </StepCard>
        </Box>
      </>)}
    </Box>
  );
};
