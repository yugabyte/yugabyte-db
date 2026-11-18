
# We can't use the official rust docker image because we need to build the
# extension against the oldest glibc version available.
# So we use Rocky8 as the base image, which ships glibc 2.28
# Our hope is that an extension built against an old version of glibc will
# continue to work against newer versions of glibc.
#FROM rust:1

FROM rockylinux:8 AS rhel8_devtools

COPY docker/pgrx/goreleaser.repo /etc/yum.repos.d/goreleaser.repo

# https://github.com/pgcentralfoundation/pgrx?tab=readme-ov-file#system-requirements
RUN dnf install -y 'dnf-command(config-manager)' \
 && dnf config-manager -y --set-enabled powertools \
 && dnf groupinstall -y 'Development Tools' \
 && dnf install -y epel-release \
 && dnf install -y \
      cmake \
      git \
      clang \
      nfpm \
      bison-devel \
      libicu-devel \
      readline-devel \
      zlib-devel \
      openssl-devel \
      ccache \
      wget \
  && dnf clean all \
  && rm -rf /var/cache/yum


FROM rhel8_devtools AS rhel8_rust

# Create the postgres user with the given uid/gid
# If you're not using Docker Desktop and your UID / GID is not 1000 then
# you'll get permission errors with volumes.
#
# You can fix that by rebuilding the image locally with:
#
# docker compose build --build-arg UID=`id -u` --build-arg GID=`id- g`
#

ARG UID=1000
ARG GID=1000
RUN groupadd -g "${GID}" postgres \
 && useradd --create-home --no-log-init -u "${UID}" -g "${GID}" postgres

USER postgres
ENV USER=postgres

# Install Rust

RUN wget https://sh.rustup.rs -O /tmp/install-rust.sh \
 && chmod +x /tmp/install-rust.sh \
 && /tmp/install-rust.sh -y

ENV PATH="/home/postgres/.cargo/bin/:${PATH}"


FROM rhel8_rust AS rhel8_pgrx

ARG PGRX_VERSION=0.17.0

RUN rustup self update \
 && rustup component add clippy llvm-tools-preview \
 && cargo install grcov \
 && cargo install --locked --version "${PGRX_VERSION}" cargo-pgrx \
 && cargo pgrx init

ENV CARGO_HOME=~postgres/.cargo/


WORKDIR /pgrx
VOLUME /pgrx
