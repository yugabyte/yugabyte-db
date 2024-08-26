FROM --platform=linux/amd64 mcr.microsoft.com/mirror/docker/library/ubuntu:20.04
ARG POSTGRES_INSTALL_ARG=
ARG PG_VERSION=16

# declare installed PG version
# ENV PG_VERSION=16

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
    && rm -rf /var/lib/apt/lists/*

# Add pgdg repo
RUN echo "deb http://apt.postgresql.org/pub/repos/apt/ $(lsb_release -cs)-pgdg main" | tee /etc/apt/sources.list.d/pgdg.list && \
    wget --quiet -O - https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add - && \
    apt-get update && \
    rm -rf /var/lib/apt/lists/*

RUN curl -4sSf https://packagecloud.io/install/repositories/citusdata/community/script.deb.sh | bash

RUN apt-get update && DEBIAN_FRONTEND=noninteractive \
    && apt-get install -y \
    postgresql-16 \
    postgresql-server-dev-16 \
    libpq-dev \
    libicu-dev \
    libkrb5-dev \
    postgresql-16-cron \
    postgresql-16-pgvector \
    postgresql-16-postgis-3 \
    postgresql-16-rum \
    &&  rm -rf /var/lib/apt/lists/*

# Install Libbson, decimal128, pcre
ENV CLEANUP_SETUP=1
ENV INSTALL_DEPENDENCIES_ROOT=/tmp/install_setup
RUN mkdir -p /tmp/install_setup
COPY scripts/ /tmp/install_setup
RUN MAKE_PROGRAM=cmake /tmp/install_setup/install_setup_libbson.sh
RUN /tmp/install_setup/install_setup_pcre2.sh
RUN /tmp/install_setup/install_setup_intel_decimal_math_lib.sh
RUN /tmp/install_setup/install_citus_indent.sh


RUN rm -rf /var/lib/apt/lists/* \
    && localedef -i en_US -c -f UTF-8 -A /usr/share/locale/locale.alias en_US.UTF-8

ENV LANG en_US.UTF-8
ENV LC_ALL en_US.UTF-8
ENV LANGUAGE en_US



RUN useradd -ms /bin/bash heliodb -G sudo
RUN echo "%sudo ALL=(ALL:ALL) NOPASSWD: ALL" >> /etc/sudoers.d/no-pass-ask

USER heliodb
WORKDIR /home/heliodb
