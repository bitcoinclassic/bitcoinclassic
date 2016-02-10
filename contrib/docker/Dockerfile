FROM ubuntu:trusty
MAINTAINER Pedro Pinheiro <pedro@blockchain.com>

ENV EXPECTED_HASH 3f4eb95a832c205d1fe3b3f4537df667f17f3a6be61416d11597feb666bde4ca

EXPOSE 8332 8333

RUN apt-get update
RUN apt-get install -y wget

RUN adduser --home /home/bitcoin --shell /bin/bash --gecos "Bitcoin" --disabled-password bitcoin

USER bitcoin
WORKDIR /home/bitcoin
ENV HOME /home/bitcoin

RUN wget https://github.com/bitcoinclassic/bitcoinclassic/releases/download/v0.11.2.cl1/bitcoin-0.11.2-linux64.tar.gz
RUN echo $EXPECTED_HASH bitcoin-0.11.2-linux64.tar.gz | sha256sum -c -
RUN tar zxvf bitcoin-0.11.2-linux64.tar.gz

ADD ./bin /home/bitcoin/bin

CMD ["bin/start.sh"]