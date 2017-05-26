FROM mono:4.2.3.4

RUN mkdir -p code
COPY bin/Release/ /code
COPY run.sh /code

ENTRYPOINT ["/code/run.sh"]
