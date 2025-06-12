FROM ubuntu:22.04

ENV DEBIAN_FRONTEND="noninteractive"
ENV TZ="Europe/Istanbul"

ARG PG_MAJOR=17

# install deps
RUN apt-get update && apt-get -y install build-essential libreadline-dev zlib1g-dev      \
                                         flex bison libxml2-dev libxslt-dev libssl-dev   \
                                         libxml2-utils xsltproc ccache pkg-config wget   \
                                         curl lsb-release ca-certificates gnupg sudo git \
                                         nano net-tools awscli

# install azure-cli
RUN curl -sL https://packages.microsoft.com/keys/microsoft.asc | gpg --dearmor | tee /etc/apt/keyrings/microsoft.gpg > /dev/null
RUN echo "deb [arch=`dpkg --print-architecture` signed-by=/etc/apt/keyrings/microsoft.gpg] https://packages.microsoft.com/repos/azure-cli/ `lsb_release -cs` main" | tee /etc/apt/sources.list.d/azure-cli.list
RUN apt-get update && apt-get install -y azure-cli

# install Postgres
RUN sh -c 'echo "deb https://apt.postgresql.org/pub/repos/apt $(lsb_release -cs)-pgdg main" > /etc/apt/sources.list.d/pgdg.list'
RUN wget --quiet -O - https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add -
RUN apt-get update && apt-get -y install postgresql-${PG_MAJOR}-postgis-3  \
                                         postgresql-server-dev-${PG_MAJOR} \
                                         postgresql-client-${PG_MAJOR}     \
                                         libpq-dev

# set up permissions so that rust user can create extensions
RUN chmod a+rwx `pg_config --pkglibdir`          \
                `pg_config --sharedir`/extension \
                /var/run/postgresql/

# initdb requires non-root user. This will also be the user that runs the container.
ARG USERNAME=rust
ARG USER_UID=1000
ARG USER_GID=1000
RUN groupadd --gid $USER_GID $USERNAME
RUN useradd --uid $USER_UID --gid $USER_GID -s /bin/bash -m $USERNAME

RUN echo "$USERNAME ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/$USERNAME

USER $USERNAME

# install Rust environment
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | bash -s -- -y
ENV PATH="/home/rust/.cargo/bin:${PATH}"

# install and configure pgrx
ARG PGRX_VERSION=0.14.1
RUN cargo install --locked cargo-pgrx@${PGRX_VERSION}
RUN cargo pgrx init --pg${PG_MAJOR} $(which pg_config)
RUN echo "shared_preload_libraries = 'pg_parquet'" >> $HOME/.pgrx/data-${PG_MAJOR}/postgresql.conf

# required for pgrx to work
ENV USER=$USERNAME

# git completion
RUN curl -o ~/.git-completion.bash https://raw.githubusercontent.com/git/git/master/contrib/completion/git-completion.bash
RUN echo "source ~/.git-completion.bash" >> ~/.bashrc
