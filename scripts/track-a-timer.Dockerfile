# LOCAL A/B BENCHMARK ONLY. This is not a scored Track A Dockerfile.
# Build from scripts/ context after building a pinned Track A solution image.
ARG SOLUTION_IMAGE
FROM rust:1-slim-trixie@sha256:1a939748a7cbab172de91a014e3804a97af1394a8c844faa67fce9401c4e40fb AS build
WORKDIR /timer
COPY track_a_timer.rs .
RUN rustc --edition=2021 -C opt-level=2 -C panic=abort -o bench-timer track_a_timer.rs

FROM ${SOLUTION_IMAGE}
COPY --from=build /timer/bench-timer /bench-timer
ENTRYPOINT ["/bench-timer"]
