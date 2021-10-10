ARG pgtag
FROM postgres:${pgtag:-alpine}
ENV POSTGRES_HOST_AUTH_METHOD trust

RUN apk --no-cache add make perl-dev patch \
 && cpan -T TAP::Parser::SourceHandler::pgTAP
ENV PGUSER postgres

WORKDIR /pgtap
ENV PATH /pgtap/test:$PATH
