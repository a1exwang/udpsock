#!/bin/bash
set -xe
docker run --name udpsock --rm --privileged -p 1235:1235/udp -it a1exwang/udpsock
