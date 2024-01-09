/*
 * Created on Tue Jul 25 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useSelector } from "react-redux";
import { Tab } from "react-bootstrap";
import { WithRouterProps, withRouter } from "react-router";
import { YBTabsPanel } from "../../../components/panels";
import ManageRoles from "./roles/components/ManageRoles";
import { ManageUsers } from "./users/components/ManageUsers";
import UserAuthContainer from "../../../components/users/UserAuth/UserAuthContainer";

const RBACComponent = (props: WithRouterProps) => {
    const activeTab = props.params.section;
    const currentUserInfo = useSelector((state: any) => state.customer.currentUser?.data);
    const role = currentUserInfo?.role;
    const isAdmin = ['SuperAdmin'].includes(role);

    return (
        <YBTabsPanel
            activeTab={activeTab}
            defaultTab={'users'}
            id="rbac-tab-panel"
            className="config-tabs"
        >
            <Tab
                eventKey="users"
                title="Users"
                unmountOnExit
            >
                <ManageUsers routerProps={props} />
            </Tab>
            <Tab
                eventKey="role"
                title={"Roles"}
                unmountOnExit
            >
                <ManageRoles />
            </Tab>
            <Tab
                eventKey="user-auth"
                title={"User Authentication"}
                unmountOnExit
            >
                <UserAuthContainer isAdmin={isAdmin} />
            </Tab>
        </YBTabsPanel>
    );
};

export const RBACContainer = withRouter(RBACComponent);
