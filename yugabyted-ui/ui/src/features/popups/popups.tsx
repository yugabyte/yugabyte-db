import React, { FC, ReactNode } from "react";
import {
  AXIOS_INSTANCE,
  useGetClusterTablesQuery,
  GetClusterTablesApiEnum,
  useGetClusterQuery,
} from '@app/api/src';
import { Placement, TooltipRenderProps } from 'react-joyride';
import { Box, Divider, Link, makeStyles, Typography } from '@material-ui/core';
import { Paper, Button } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import clsx from 'clsx';
import CloseIcon from '@app/assets/close.svg';
import CloseImage2 from '@app/assets/close2.svg';
import SlackGraphic from '@app/assets/slack.png';
import GithubGraphic from '@app/assets/github.png';
import ShirtGraphic from '@app/assets/tshirt.png';
import G2Graphic from '@app/assets/g2usersloveus.png';

// Callhome URL
export const CALLHOME_URL = "/callhome";

// Tracking links for popups that point to existing links in the UI.
// This allows us to track clicks when popup has been shown, and clicks where popups have
// not been shown.
export const LINK_SLACK = "https://inviter.co/yugabytedb";
export const LINK_GITHUB = "https://github.com/yugabyte/yugabyte-db"
export const LINK_TSHIRT = "https://www.yugabyte.com/community-rewards/";
export const LINK_G2 = "https://www.g2.com/products/yugabytedb/reviews";

export const POPUP_KEY_SLACK = "join_slack";
export const POPUP_KEY_GITHUB = "github_star";
export const POPUP_KEY_TSHIRT = "claim_tshirt";
export const POPUP_KEY_G2 = "g2_review";

// Config object for popups.
// Priority: If two popups are eligible, the one with a higher priority value will be shown.
// Popups with the same priority will be chosen from randomly.

export function getPopupConfig(): {[key: string]: {[key: string]: any}} {
    const { t } = useTranslation();

    return {
        // Popups for root (i.e. overview) page
        "/":
        {
            "header_slack":
            [
                {
                    "metric_names": "cluster_uptime_ms,total_user_tables",
                    "metric_values": "86400000,1",
                    "message_title": t("popups.join_slack.message_title"),
                    "message_text": t("popups.join_slack.message_text"),
                    "key": POPUP_KEY_SLACK,
                    "link": LINK_SLACK,
                    "link_text": t("popups.join_slack.link_text"),
                    "priority": 15,
                    // Will show popup if query param "tab" is absent or has value tabOverview
                    "query_parameters": {
                        "tab": [
                            null,
                            "tabOverview"
                        ],
                    },
                }
            ],
            "header_github":
            [
                {
                    "metric_names": "cluster_uptime_ms,total_user_tables",
                    "metric_values": "86400000,3",
                    "message_title": t("popups.github_star.message_title"),
                    "message_text": t("popups.github_star.message_text"),
                    "key": POPUP_KEY_GITHUB,
                    "link": LINK_GITHUB,
                    "link_text": t("popups.github_star.link_text"),
                    "priority": 15,
                    "query_parameters": {
                        "tab": [
                            null,
                            "tabOverview"
                        ],
                    },
                }
            ],
            "sidebar_tshirt":
            [
                {
                    "metric_names": "total_user_tables",
                    "metric_values": "1",
                    "message_title": t("popups.claim_tshirt.message_title"),
                    "message_text": t("popups.claim_tshirt.message_text"),
                    "key": POPUP_KEY_TSHIRT,
                    "link": LINK_TSHIRT,
                    "link_text": t("popups.claim_tshirt.link_text"),
                    "priority": 10,
                    "query_parameters": {
                        "tab": [
                            null,
                            "tabOverview"
                        ],
                    },
                }
            ],
            "page":
            [
                {
                    "metric_names": "cluster_uptime_ms,total_user_tables,num_nodes",
                    "metric_values": "7889238000,5,3",
                    "message_title": t("popups.g2_review.message_title"),
                    "message_text": t("popups.g2_review.message_text"),
                    "key": POPUP_KEY_G2,
                    "link": LINK_G2,
                    "link_text": t("popups.g2_review.link_text"),
                    "priority": 20,
                    "query_parameters": {
                        "tab": [
                            null,
                            "tabOverview"
                        ],
                    },
                }
            ]
        }
    };
};

// Metric definitions for popups
export function useMetricNameToFunctionMap():
  {map: {[key: string]: (metricValue: number) => boolean}, isLoading: boolean} {
  // Get all data required for metrics
  const { data: clusterTablesResponseYsql, isLoading: isLoadingTablesYsql } =
    useGetClusterTablesQuery({ api: GetClusterTablesApiEnum.Ysql });
  const { data: clusterTablesResponseYcql, isLoading: isLoadingTablesYcql } =
    useGetClusterTablesQuery({ api: GetClusterTablesApiEnum.Ycql });
  const { data: clusterData, isLoading: isLoadingClusterData } = useGetClusterQuery();

  const isLoading = isLoadingTablesYsql || isLoadingTablesYcql || isLoadingClusterData;

  const metricNameToFunctionMap: {[key: string]: (metricValue: number) => boolean} = {
    "total_user_tables": (metricValue: number) => {
        var totalTables = (clusterTablesResponseYsql?.tables?.length ?? 0) +
            (clusterTablesResponseYcql?.tables?.length ?? 0);
        return totalTables >= metricValue;
    },
    "cluster_uptime_ms": (metricValue: number) => {
        return Math.abs(new Date().getTime() -
            new Date(String(clusterData?.data?.info.metadata.created_on)).getTime()) >= metricValue;
    },
    "num_nodes": (metricValue: number) => {
        return (clusterData?.data?.spec?.cluster_info?.num_nodes ?? 0) >= metricValue;
    }
  }
  return { map: metricNameToFunctionMap, isLoading: isLoading };
}

const useStyles = makeStyles((theme) => ({
  paper: {
    display: "flex",
    width: "100%",
    alignItems: "center",
    justifyContent: "space-between",
    border: "none",
    maxWidth: "536px",
    borderRadius: 16,
    zIndex: 1000,
  },
  closeBtn: {
    backgroundColor: theme.palette.grey[100],
    borderRadius: "50%",
    width: 34,
    height: 34,
    cursor: "pointer",
    float: "right",
    marginLeft: "auto",
  },
  textBox: {
    padding: theme.spacing(2.5),
    display: "flex",
    flexDirection: "column",
    justifyContent: "center",
  },
  link: {
    padding: theme.spacing(2.5, 0),
  },
  header: {
    padding: theme.spacing(2.5),
  },
  hoverUnderline: {
    color: '#888',
    cursor: 'pointer',
    '&:hover': {
      textDecoration: 'underline',
    },
  },
  closePermanent: {
    float: "right",
    marginLeft: "auto",
    display: "flex",
    color: "#888",
    padding: theme.spacing(0, 2.5, 2.5, 2.5),
  },
  graphic: {
    padding: theme.spacing(2.5, 0, 0, 2.5),
  }
}));

// Default component for popups
function GraphicComponent(graphic: ReactNode) {
    const component = (props: TooltipRenderProps) => {
        const { primaryProps, closeProps, step, tooltipProps } = props;
        const classes = useStyles();
        const { t } = useTranslation();
        return (
          <Paper className={clsx(classes.paper, "tooltip__body")} {...tooltipProps}>
            <Box display="flex" flexDirection="column">
              <Box display="flex" flexDirection="row" alignItems="center"
                className={classes.header}>
                {step.title &&
                  <Typography
                    variant="h3"
                    style={{
                      color: "#5E60F0",
                      fontWeight: 600,
                      fontSize: 18,
                    }}
                    className="tooltip__title">{step.title}
                  </Typography>}
                <Button className={clsx(classes.closeBtn, "tooltip__close")} {...closeProps}>
                  <CloseIcon/>
                </Button>
              </Box>
              <Divider
                  style={{
                    backgroundColor: '#D3D3D3',
                    boxSizing: "border-box",
                    width: 'inherit',
                  }}
                />
              <Box display="flex" flexDirection="row">
                <Box className={classes.graphic}>
                  {graphic}
                </Box>
                <Box className={classes.textBox}>
                  <Typography variant="body2" className="tooltip__content">
                    {step.content}
                  </Typography>
                  {step.data.link &&
                  <Box className={clsx(classes.link, "tooltip__content")}>
                    <PopupCallhomeLink
                      popup_key={step.data.key}
                      popup_link={step.data.link}
                      popup_link_text={step.data.link_text}/>
                  </Box>}
                </Box>
              </Box>
              <Box
                className={clsx(classes.closePermanent, "tooltip__button tooltip__button--primary")}
                {...primaryProps}
              >
                <CloseImage2/>
                <Typography variant="body2" className={classes.hoverUnderline}>
                  {t('common.modalClose')}
                </Typography>
              </Box>
            </Box>
          </Paper>
        );
    }
    return component;
}

const popupKeyToCustomComponent: {[key: string]: FC<TooltipRenderProps>} = {
  "join_slack": GraphicComponent(<img width={100} src={SlackGraphic}/>),
  "github_star": GraphicComponent(<img width={100} src={GithubGraphic}/>),
  "claim_tshirt": GraphicComponent(<img width={100} src={ShirtGraphic}/>),
  "g2_review": GraphicComponent(<img width={100} src={G2Graphic}/>),
};

export function getCustomComponentFromPopupKey(key: string): FC<TooltipRenderProps> {
  return popupKeyToCustomComponent[key] ?? GraphicComponent(null);
}

// Maps from popup location string to actual CSS selectors
const popupLocationToTarget: {[key: string]: string} = {
  "header_slack": "#header_slack",
  "header_github": "#header_github",
  "sidebar_tshirt": "#sidebar_tshirt",
  "page": "#header_help",
};

export function getTargetFromPopupLocation(key: string): string {
  return popupLocationToTarget[key] ?? "body";
}

// Maps from popup location to placement location relative to CSS element
const popupLocationToPlacement:
    {[key: string]: "auto" | Placement | "center" | undefined} = {
  "header_slack": "auto",
  "header_github": "auto",
  "sidebar_tshirt": "top",
  "page": "bottom",
};

export function getPlacementFromPopupLocation(key: string):
    "auto" | Placement | "center" | undefined {
  return popupLocationToPlacement[key] ?? "auto";
}

// If popup at placement location should have arrow pointing to attached element
const popupLocationShouldHideArrow: {[key: string]: boolean} = {
  "page": true,
}

export function shouldHideArrow(key: string): boolean {
  return popupLocationShouldHideArrow[key] ?? false;
}

export function linkClickCallhome(popup_key: string, popup_link: string) {
    var localStorageObject = getLocalStorageJSON(popup_key);
    var status = localStorageObject["status"] || null;
    var closed_timestamp = localStorageObject["closed_timestamp"];
    var time_since_closed_sec = Math.floor(Date.now() / 1000) - closed_timestamp;

    var data = {
      "popup_event": {
        "event": "yugabyted_ui_link_click",
        "key": popup_key,
        "link": popup_link,
        "popup_status": status,
        "time_since_closed_sec": time_since_closed_sec,
      }
    };
    AXIOS_INSTANCE.post(CALLHOME_URL, data);
}

type PopupCallhomeLinkProps = {
  popup_key: string;
  popup_link: string;
  popup_link_text?: string;
};

export const PopupCallhomeLink: FC<PopupCallhomeLinkProps> =
  ({ popup_key, popup_link, popup_link_text }) => {
  return (
    <Link
      href={popup_link}
      target="_blank"
      className="tooltip__content"
      onClick={() => linkClickCallhome(popup_key, popup_link)}>
        {popup_link_text ?? popup_link}
    </Link>
  );
}

// Attempt to get JSON object from string stored in localStorage
// Will delete localstorage contents if not valid JSON
// Returns the empty object {} if not found
export const getLocalStorageJSON: any = (key: string) => {
  var localStorageString = localStorage.getItem(key);
  if (localStorageString === null) {
    return {};
  }
  try {
    return JSON.parse(localStorageString);
  } catch {
    // Remove bad object from storage and don't show the popup
    localStorage.removeItem(key);
    return {};
  }
}

// Store object in localStorage by calling JSON.stringify
// Do nothing if fails
export const setLocalStorageJSON = (key: string, object: any) => {
  try {
    var localStorageString = JSON.stringify(object);
    localStorage.setItem(key, localStorageString);
  } catch (err: any) {
    console.log("Failed to save localStorage key " + key + ":", object);
    if (err instanceof Error) {
        console.log("Error: " + err.message)
    }
  }
}
