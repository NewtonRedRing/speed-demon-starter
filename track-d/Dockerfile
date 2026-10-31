FROM gcc:15@sha256:eb5e64286ad47187b32bda75c6b6916166ecb41da56559b8b09e0001748d2cce AS build
WORKDIR /app
COPY src/main.cpp .
ARG SD_TUNE_WORKERS=12
ARG SD_TUNE_PARAM=256
ARG SD_TUNE_EXTRA_FLAGS=""
RUN g++ -std=c++20 -O3 -DNDEBUG -flto -march=native -pthread -static -s \
    -DSD_TUNE_WORKERS=${SD_TUNE_WORKERS} -DSD_TUNE_PARAM=${SD_TUNE_PARAM} \
    ${SD_TUNE_EXTRA_FLAGS} -o solution main.cpp

FROM debian:13-slim@sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad
COPY --from=build /app/solution /app/solution
ENTRYPOINT ["/app/solution"]
