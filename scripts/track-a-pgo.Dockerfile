# OPTIONAL local Track A PGO experiment. Not used by the scored track-a/Dockerfile.
# Invocation (from repository root):
#   docker build --network=none -t speeddemon-pgo/track-a \
#     -f scripts/track-a-pgo.Dockerfile track-a
#   python3 check.py a --skip-build --image speeddemon-pgo/track-a
# Always test the downloaded ORIGINAL public seed-1 full dataset in check.py.
FROM python:3.13-slim@sha256:d98cc29d3266e535dd9da1099688d9cb3ca4753813571e071763d17725460962 AS training
WORKDIR /gen
COPY pgo_gen.py .
RUN python3 pgo_gen.py --seed 2 --scale tiny --out /train/tiny && \
    python3 pgo_gen.py --seed 3 --scale sample --out /train/sample

FROM halloumee/speed-demon-rust:1.99.1-trixie-llvm-tools@sha256:13e4ee7135d8eac6c56476c7d1aee201f36479f4745fd5d0e6b4529f4e13251a AS build
WORKDIR /app
COPY main.rs .
COPY --from=training /train /train
RUN mkdir -p /out /prof && \
    rustc --edition=2021 -C opt-level=3 -C lto=fat -C target-cpu=native -C panic=abort -C codegen-units=1 \
      -C profile-generate=/prof -o training-bin main.rs && \
    ln -s /train/tiny /data && \
    LLVM_PROFILE_FILE=/prof/tiny_%m_%p.profraw ./training-bin && \
    rm /data && \
    ln -s /train/sample /data && \
    LLVM_PROFILE_FILE=/prof/sample_%m_%p.profraw ./training-bin && \
    rm /data && \
    llvm-profdata merge -o /prof/merged.profdata /prof/*.profraw && \
    rustc --edition=2021 -C opt-level=3 -C lto=fat -C target-cpu=native -C panic=abort -C codegen-units=1 \
      -C profile-use=/prof/merged.profdata -C llvm-args=-pgo-warn-missing-function -o solution main.rs

FROM debian:13-slim@sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad
COPY --from=build /app/solution /app/solution
ENTRYPOINT ["/app/solution"]
