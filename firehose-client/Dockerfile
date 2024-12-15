FROM ubuntu:24.04 as build

WORKDIR /usr/src/firehose-client

COPY . .
RUN cmake --preset-Release-GCC && cmake --build --preset=rel-gcc

# final stage

FROM ubuntu:24.04

WORKDIR /usr/src/firehose-client
COPY --from=build /usr/src/firehose-client /usr/src/firehose-client

ENTRYPOINT ["./firehose_client"]
EXPOSE 9091
ENV EXPOSER_PORT=9091
CMD ["./config.yml"]

LABEL org.opencontainers.image.source=https://github.com/SteveTownsend/nafo-forum-moderation
LABEL org.opencontainers.image.description="NAFO Moderation Firehose Client"
LABEL org.opencontainers.image.licenses=MIT
