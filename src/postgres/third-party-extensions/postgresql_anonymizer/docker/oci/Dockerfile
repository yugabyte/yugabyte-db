# This instructions must be declared before any FROM
# You can override it and build your own image based another version
# like this: `PG_MAJOR_VERSION=13 make docker_image`
ARG PG_MAJOR_VERSION=18
ARG ANON_MINOR_VERSION=2.5.1
ARG OCI_DISTRO=trixie
ARG OCI_ARCH=amd64

FROM dalibo/buildpack-rust:trixie AS builder

ARG PG_MAJOR_VERSION=18
ARG ANON_MINOR_VERSION=2.5.1
ARG OCI_ARCH=amd64

# Bug with PG18
RUN apt-get purge -y postgresql-client-18

RUN apt-get update \
 && apt-get install -y \
      postgresql-${PG_MAJOR_VERSION} \
      postgresql-server-dev-${PG_MAJOR_VERSION} \
 && rm -rf /var/lib/apt/lists/*

RUN cargo pgrx init --pg${PG_MAJOR_VERSION} /usr/lib/postgresql/${PG_MAJOR_VERSION}/bin/pg_config

COPY Makefile Cargo.toml anon.control nfpm.template.yaml ./
COPY sql ./sql
COPY src ./src


RUN make deb PGVER=pg${PG_MAJOR_VERSION}

ENV DEB=target/release/anon-pg${PG_MAJOR_VERSION}/*.deb

RUN dpkg-deb --extract ${DEB} /tmp \
 && rm ${DEB}

FROM scratch

COPY --from=builder /tmp/usr/lib/ /lib/
COPY --from=builder /tmp/usr/share/ /share/
