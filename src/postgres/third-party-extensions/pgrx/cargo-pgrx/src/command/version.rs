//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx_pg_config::{PgConfig, Pgrx, SUPPORTED_VERSIONS};

pub(crate) fn pgrx_default() -> eyre::Result<Pgrx> {
    let mut pgrx = Pgrx::default();

    rss::PostgreSQLVersionRss::new(&SUPPORTED_VERSIONS())?
        .into_iter()
        .for_each(|version| pgrx.push(PgConfig::from(version)));

    Ok(pgrx)
}

mod rss {
    use eyre::WrapErr;
    use owo_colors::OwoColorize;
    use pgrx_pg_config::{PgMinorVersion, PgVersion};
    use serde::Deserialize;
    use std::collections::BTreeMap;
    use std::io::Read;
    use url::Url;

    use crate::command::build_agent_for_url;

    #[cfg(not(target_os = "windows"))]
    fn download_url(major: u16, minor: u16) -> String {
        format!("https://ftp.postgresql.org/pub/source/v{major}.{minor}/postgresql-{major}.{minor}.tar.bz2")
    }

    #[cfg(target_os = "windows")]
    fn download_url(major: u16, minor: u16) -> String {
        format!("https://get.enterprisedb.com/postgresql/postgresql-{major}.{minor}-1-windows-x64-binaries.zip")
    }

    pub(super) struct PostgreSQLVersionRss;

    impl PostgreSQLVersionRss {
        pub(super) fn new(supported_versions: &[PgVersion]) -> eyre::Result<Vec<PgVersion>> {
            static VERSIONS_RSS_URL: &str = "https://www.postgresql.org/versions.rss";

            let http_client = build_agent_for_url(VERSIONS_RSS_URL)?;
            let mut response = http_client
                .get(VERSIONS_RSS_URL)
                .call()
                .wrap_err_with(|| format!("unable to retrieve {VERSIONS_RSS_URL}"))?;

            let mut buf = Vec::new();
            let _count = response.body_mut().as_reader().read_to_end(&mut buf)?;
            let body = String::from_utf8(buf)?;
            let rss: Rss = match serde_xml_rs::from_str(&body) {
                Ok(rss) => rss,
                Err(e) => return Err(e.into()),
            };

            let mut versions: BTreeMap<u16, PgVersion> = BTreeMap::from_iter(
                supported_versions.iter().map(|pgver| (pgver.major, pgver.clone())),
            );

            for item in rss.channel.item {
                let title = item.title.trim();
                let mut parts = title.split('.');
                let major = parts.next();
                let minor = parts.next();

                // if we don't have major/minor versions or if they don't parse correctly
                // we'll just assume zero for them and eventually skip them
                let major = major.unwrap().parse::<u16>().unwrap_or_default();
                let minor = minor.unwrap().parse::<u16>().unwrap_or_default();

                if let Some(known_pgver) = versions.get_mut(&major) {
                    if matches!(known_pgver.minor, PgMinorVersion::Latest) {
                        // fill in the latest minor version number and its url
                        known_pgver.minor = PgMinorVersion::Release(minor);
                        known_pgver.url = Some(Url::parse(&download_url(major, minor))?);
                    }
                }
            }

            println!(
                "{} Postgres {}",
                "   Discovered".white().bold(),
                versions.iter().map(|ver| format!("v{}", ver.1)).collect::<Vec<_>>().join(", ")
            );

            Ok(versions.into_values().collect())
        }
    }

    #[derive(Deserialize)]
    struct Rss {
        channel: Channel,
    }

    #[derive(Deserialize)]
    struct Channel {
        item: Vec<Item>,
    }

    #[derive(Deserialize)]
    struct Item {
        title: String,
    }
}
