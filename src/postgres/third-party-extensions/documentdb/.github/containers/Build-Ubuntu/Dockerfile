FROM mcr.microsoft.com/mirror/docker/library/ubuntu:22.04
ARG POSTGRES_INSTALL_ARG=
ARG PG_VERSION=16
ARG CITUS_VERSION=12

# declare installed PG version and Citus version
ENV PG_VERSION=${PG_VERSION}
ENV CITUS_VERSION=${CITUS_VERSION}

# Install build essentials - Compiler, debugger, make, etc.
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get install -qy \
    wget \
    curl \
    sudo \
    gnupg2 \
    lsb-release \
    tzdata \
    build-essential \
    pkg-config \
    cmake \
    git \
    locales \
    gcc \
    gdb \
    libipc-run-perl \
    unzip \
    apt-transport-https \
    bison \
    flex \
    libreadline-dev \
    zlib1g-dev \
    libkrb5-dev \
    software-properties-common \
    libtool \
    libicu-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Add pgdg repo
RUN echo "deb http://apt.postgresql.org/pub/repos/apt/ $(lsb_release -cs)-pgdg main" | tee /etc/apt/sources.list.d/pgdg.list && \
    wget --quiet -O - https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add - && \
    apt-get update && \
    rm -rf /var/lib/apt/lists/*

# Prepare for running install scripts
ENV CLEANUP_SETUP=1
ENV INSTALL_DEPENDENCIES_ROOT=/tmp/install_setup
RUN mkdir -p /tmp/install_setup

# Copy setup_versions.sh which decides versions of the dependencies to install.
COPY scripts/setup_versions.sh /tmp/install_setup/

# Install libbson
COPY scripts/install_setup_libbson.sh /tmp/install_setup
RUN [ "bin/bash", "-c", "export MAKE_PROGRAM=cmake && /tmp/install_setup/install_setup_libbson.sh" ]

# Copy utils.sh
COPY scripts/utils.sh /tmp/install_setup/

# Install postgres
COPY scripts/install_setup_postgres.sh /tmp/install_setup/
RUN [ "bin/bash", "-c", "/tmp/install_setup/install_setup_postgres.sh -d \"/usr/lib/postgresql/${PG_VERSION}\" $POSTGRES_INSTALL_ARG -v ${PG_VERSION}" ]

# Install RUM from source (only for PG versions < 18)
COPY scripts/install_setup_rum_oss.sh /tmp/install_setup/
RUN [ "bin/bash", "-c", "if [ \"${PG_VERSION:-0}\" -lt 18 ]; then PGVERSION=$PG_VERSION /tmp/install_setup/install_setup_rum_oss.sh; else echo 'Skipping public RUM install for PG_VERSION' ${PG_VERSION}; fi" ]

# Install citus
COPY scripts/install_setup_citus_core_oss.sh /tmp/install_setup/
RUN [ "bin/bash", "-c", "PGVERSION=$PG_VERSION /tmp/install_setup/install_setup_citus_core_oss.sh ${CITUS_VERSION}" ]

# Install citus-indent
COPY scripts/install_citus_indent.sh /tmp/install_setup/
RUN [ "bin/bash", "-c", "/tmp/install_setup/install_citus_indent.sh" ]

# Install contrib extensions
COPY scripts/install_setup_contrib_extensions_core.sh /tmp/install_setup/
COPY scripts/install_setup_contrib_extensions.sh /tmp/install_setup/
RUN [ "bin/bash", "-c", "PGVERSION=$PG_VERSION /tmp/install_setup/install_setup_contrib_extensions.sh" ]

# Install PG_CRON
COPY scripts/install_setup_pg_cron.sh /tmp/install_setup/
RUN [ "bin/bash", "-c", "PGVERSION=$PG_VERSION /tmp/install_setup/install_setup_pg_cron.sh" ]

# Download Decimal128 Intel library
COPY scripts/install_setup_intel_decimal_math_lib.sh /tmp/install_setup/
RUN [ "bin/bash", "-c", "/tmp/install_setup/install_setup_intel_decimal_math_lib.sh" ]

# Download PCRE2 library
COPY scripts/install_setup_pcre2.sh /tmp/install_setup/
RUN [ "bin/bash", "-c", "/tmp/install_setup/install_setup_pcre2.sh" ]

# Install PG_VECTOR
COPY scripts/install_setup_pgvector.sh /tmp/install_setup/
RUN [ "bin/bash", "-c", "PGVERSION=$PG_VERSION /tmp/install_setup/install_setup_pgvector.sh" ]

# Install PostGIS from source
RUN add-apt-repository -y ppa:ubuntugis/ppa && apt-get update && \
    apt-get install -qy \
    libproj-dev \
    libxml2-dev \
    libjson-c-dev \
    libgeos-dev \
    libgeos++-dev \
    && rm -rf /var/lib/apt/lists/*
COPY scripts/install_setup_postgis.sh /tmp/install_setup/
RUN [ "bin/bash", "-c", "PGVERSION=$PG_VERSION /tmp/install_setup/install_setup_postgis.sh" ]

# locale
RUN rm -rf /var/lib/apt/lists/* \
    && localedef -i en_US -c -f UTF-8 -A /usr/share/locale/locale.alias en_US.UTF-8

ENV LANG en_US.UTF-8
ENV LC_ALL en_US.UTF-8
ENV LANGUAGE en_US

# Create documentdb user
RUN useradd -ms /bin/bash documentdb -G sudo
RUN echo "%sudo ALL=(ALL:ALL) NOPASSWD: ALL" >> /etc/sudoers.d/no-pass-ask

# Add postgres to path for sudo commands.
# Add path for sudoers
RUN cat /etc/sudoers | grep secure_path | sed "s/\:\/bin\:/\:\/bin\:\/usr\/lib\/postgresql\/$PG_VERSION\/bin\:/" >> /etc/sudoers.d/postgres_path

# Add PG to the path
ENV PATH=$PATH:/usr/lib/postgresql/$PG_VERSION/bin

USER documentdb
WORKDIR /home/documentdb

LABEL org.opencontainers.image.source=https://github.com/microsoft/documentdb
LABEL org.opencontainers.image.description="DocumentDB ubuntu build image"
LABEL org.opencontainers.image.licenses=MIT