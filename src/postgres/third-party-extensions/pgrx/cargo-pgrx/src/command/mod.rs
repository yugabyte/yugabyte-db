//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use env_proxy::for_url_str;
use ureq::{Agent, Proxy};

pub(crate) mod connect;
pub(crate) mod cross;
pub(crate) mod get;
pub(crate) mod info;
pub(crate) mod init;
pub(crate) mod install;
pub(crate) mod new;
pub(crate) mod package;
pub(crate) mod pgrx;
pub(crate) mod run;
pub(crate) mod schema;
pub(crate) mod start;
pub(crate) mod status;
pub(crate) mod stop;
pub(crate) mod sudo_install;
pub(crate) mod test;
pub(crate) mod upgrade;
pub(crate) mod version;

// Build a ureq::Agent by the given url. Requests from this agent are proxied if we have
// set the HTTPS_PROXY/HTTP_PROXY environment variables.
fn build_agent_for_url(url: &str) -> eyre::Result<Agent> {
    if let Some(proxy_url) = for_url_str(url).to_string() {
        let config = Agent::config_builder().proxy(Some(Proxy::new(&proxy_url)?)).build();
        Ok(Agent::new_with_config(config))
    } else {
        Ok(Agent::new_with_defaults())
    }
}

// TODO: Abstract over the repeated `fn perform`?
