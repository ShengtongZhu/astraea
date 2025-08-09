export CC = g++

export CCFLAGS = -I./net -I../third_party/json/single_include/nlohmann -std=c++17 -Wall -pedantic -Wextra -Weffc++ -g
export LDFLAGS = -pthread -lm -lstdc++ -lstdc++fs

.PHONY: all clean

all: libnet.a client server client_eval client_receiver server_sender passive_client

# Build the net library first
libnet.a:
	$(MAKE) -C net all

# Main executables
client: client.cc libnet.a
	$(CC) client.cc $(CCFLAGS) $(LDFLAGS) -o client -L./net -lnet

server: server.cc libnet.a
	$(CC) server.cc $(CCFLAGS) $(LDFLAGS) -o server -L./net -lnet

client_eval: client_eval.cc libnet.a
	$(CC) client_eval.cc $(CCFLAGS) $(LDFLAGS) -o client_eval -L./net -lnet

client_receiver: client_receiver.cc libnet.a
	$(CC) client_receiver.cc $(CCFLAGS) $(LDFLAGS) -o client_receiver -L./net -lnet

server_sender: server_sender.cc libnet.a
	$(CC) server_sender.cc $(CCFLAGS) $(LDFLAGS) -o server_sender -L./net -lnet

passive_client: passive_client.cc libnet.a
	$(CC) passive_client.cc $(CCFLAGS) $(LDFLAGS) -o passive_client -L./net -lnet

# Optional batch evaluation clients (require inference service)
client_eval_batch: client_eval_batch.cc libnet.a
	$(CC) client_eval_batch.cc $(CCFLAGS) $(LDFLAGS) -o client_eval_batch -L./net -lnet

client_eval_batch_udp: client_eval_batch_udp.cc libnet.a
	$(CC) client_eval_batch_udp.cc $(CCFLAGS) $(LDFLAGS) -o client_eval_batch_udp -L./net -lnet

clean:
	$(MAKE) -C net clean
	-rm -f client server client_eval client_receiver server_sender passive_client client_eval_batch client_eval_batch_udp
