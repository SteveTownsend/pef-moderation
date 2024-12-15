#install build dependencies
FROM ubuntu:latest as dependencies

RUN apt-get -y update
RUN sudo apt-get install libboost-all-dev git libicu-dev libssl-dev

WORKDIR /usr/src
RUN git clone https://github.com/SteveTownsend/prometheus-cpp.git
WORKDIR /usr/src/prometheus-cpp

# fetch third-party dependencies
RUN git submodule init
RUN git submodule update
RUN mkdir _build
RUN cd _build

# run cmake
RUN cmake .. -DBUILD_SHARED_LIBS=ON -DENABLE_PUSH=OFF -DENABLE_COMPRESSION=OFF

# build
RUN cmake --build . --parallel 4

# install the libraries and headers
RUN cmake --install .

# build the firehose-client
FROM dependencies as build

WORKDIR /usr/src/firehose-client

COPY . .
RUN cmake --preset=Release-GCC && cmake --build --preset=rel-gcc

# prepare the runtime environment
FROM ubuntu:latest

WORKDIR /usr/src/firehose-client
COPY --from=build ./release-gcc/firehose_client /usr/src/firehose-client
COPY --from=build ./config /usr/src/firehose-client/config
RUN mkdir -p ./logs

ENTRYPOINT ["./firehose_client"]
EXPOSE 9091
ENV EXPOSER_PORT=9091
CMD ["./live_config.yml"]

LABEL org.opencontainers.image.source=https://github.com/SteveTownsend/nafo-forum-moderation
LABEL org.opencontainers.image.description="NAFO Moderation Firehose Client"
LABEL org.opencontainers.image.licenses=MIT
