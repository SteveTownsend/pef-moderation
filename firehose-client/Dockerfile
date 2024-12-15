FROM ubuntu:24.04 as build

WORKDIR /usr/src/firehose-client

COPY package.json yarn.lock .
RUN yarn
COPY . .
RUN yarn build
RUN rm -rf node_modules .next/cache
RUN mv service/package.json package.json && mv service/yarn.lock yarn.lock
RUN yarn

# final stage

FROM ubuntu:24.04

RUN apk add --update dumb-init
ENV TZ=Etc/UTC

WORKDIR /usr/src/firehose-client
COPY --from=build /usr/src/firehose-client /usr/src/firehose-client
RUN chown -R node:node .

ENTRYPOINT ["dumb-init", "--"]
EXPOSE 3000
ENV OZONE_PORT=3000
ENV NODE_ENV=production
USER node
CMD ["firehose_client", "./config.yml"]

LABEL org.opencontainers.image.source=https://github.com/SteveTownsend/nafo-forum-moderation
LABEL org.opencontainers.image.description="NAFO Moderation Firehose Client"
LABEL org.opencontainers.image.licenses=MIT
