FROM ubuntu:18.10
RUN apt-get update
RUN apt-get install -y libboost-all-dev cmake libssl-dev fish vim iproute2 iputils-ping gdb build-essential
RUN apt-get install -y libgoogle-glog-dev openvpn iperf3 tcpdump
ADD . /udpsock
RUN cd /udpsock && mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make

CMD set -xe && \
  openvpn --mktun --dev tun1 && \
  ip l set tun1 up && \
  ip a add dev tun1 10.23.0.2/24 && \
  /udpsock/build/udpsock --server=true --host=0.0.0.0 --port=1235
