FROM gcc:15@sha256:eb5e64286ad47187b32bda75c6b6916166ecb41da56559b8b09e0001748d2cce AS build
WORKDIR /app
COPY src/main.cpp .
# The generator is build-time only; profile output and sample rows are removed.
# Keep the training and release executable path identical so GCC finds the .gcda.
RUN mkdir -p /app/pgo /data /out \
    && g++ -std=c++20 -O3 -DNDEBUG -flto -march=native -pthread -static \
       -fprofile-generate=/app/pgo -o /app/solution main.cpp \
    && awk 'BEGIN { split("GET POST PUT PATCH DELETE HEAD OPTIONS", meth, " "); \
       for (i=0;i<180000;i++) { h=i%72; d=int((h+18)/24); mo=d<2?10:11; dd=d<2?d+30:d-1; \
       hr=(h+18)%24; ep=i%4?i%24:int(i/4)%1842; err=i%19==0; \
       printf "2026-%02d-%02dT%02d:21:31.876543Z %s /api/v2/catalogue/%04d/events/{id} %d %d\\n", \
       mo,dd,hr,meth[(i%7)+1],ep,err?503:200,(i*13973)%1000000 } }' > /data/requests.log \
    && /app/solution \
    && g++ -std=c++20 -O3 -DNDEBUG -flto -march=native -pthread -static -s \
       -fprofile-use=/app/pgo -fprofile-correction -o /app/solution main.cpp \
    && rm -rf /app/pgo /data /out

FROM debian:13-slim@sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad
COPY --from=build /app/solution /app/solution
ENTRYPOINT ["/app/solution"]
