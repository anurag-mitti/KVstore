#include <iostream>
#include <vector>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocol/binary_packet.h"

// Simulate an AI Agent doing RAG (Retrieval-Augmented Generation)
// It needs to fetch 50 document chunks from the database 10,000 times.
constexpr int RAG_PROMPTS = 10000;
constexpr int CHUNKS_PER_PROMPT = 50;

int connect_server() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    return sock;
}

void benchmark_sequential_gets() {
    int sock = connect_server();
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < RAG_PROMPTS; ++i) {
        // AI asks for 50 keys sequentially (Standard Redis/KV approach)
        for (int k = 0; k < CHUNKS_PER_PROMPT; ++k) {
            RequestPacket req;
            req.opcode = OP_GET;
            req.key = k;
            send(sock, &req, sizeof(RequestPacket), 0);
            
            ResponsePacket res;
            recv(sock, &res, sizeof(ResponsePacket), MSG_WAITALL);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    std::cout << "Sequential GETs (Traditional): " << diff.count() << " seconds.\n";
    close(sock);
}

void benchmark_context_batching() {
    int sock = connect_server();
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < RAG_PROMPTS; ++i) {
        // AI asks for 50 keys in a single Context Batch (TitanDB approach)
        ContextRequest req;
        req.opcode = OP_GET_CONTEXT;
        req.num_keys = CHUNKS_PER_PROMPT;
        for (int k = 0; k < CHUNKS_PER_PROMPT; ++k) {
            req.keys[k] = k;
        }
        
        send(sock, &req, sizeof(ContextRequest), 0);
        
        ContextResponse res;
        recv(sock, &res, sizeof(ContextResponse), MSG_WAITALL);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    std::cout << "AI Context Batching (TitanDB): " << diff.count() << " seconds.\n";
    close(sock);
}

int main() {
    std::cout << "Running AI RAG Context Retrieval Benchmark...\n";
    std::cout << "Simulating " << RAG_PROMPTS << " prompt assemblies (50 chunks each).\n\n";
    
    benchmark_sequential_gets();
    benchmark_context_batching();
    
    return 0;
}
