#!/bin/bash
echo "Building TitanDB (Real-World Engine)..."
g++ -std=c++20 -O3 -Wall -I src src/main.cpp src/net/epoll_server.cpp -o titandb_server -lpthread
echo "Build complete. Run server with: ./titandb_server"
