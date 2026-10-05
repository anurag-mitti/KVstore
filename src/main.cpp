#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <cstring>
#include <arpa/inet.h> 
#include <vector>

#include "net/epoll_server.h"
#include "store/skip_list.h"
#include "protocol/binary_packet.h"

ShardedSkipList db(256);

void process_set(int client_fd, const std::vector<char>& buf, uint32_t offset) {
    uint16_t key_len;
    std::memcpy(&key_len, buf.data() + offset, 2);
    key_len = ntohs(key_len); 
    offset += 2;
    std::string key(buf.data() + offset, key_len);
    offset += key_len;

    uint32_t val_len;
    std::memcpy(&val_len, buf.data() + offset, 4);
    val_len = ntohl(val_len);
    offset += 4;
    std::string value(buf.data() + offset, val_len);
    
    // std::cout << "setting key: " << key << std::endl;
    db.set(key, value);
    
    std::vector<char> res(5);
    uint32_t net_len = htonl(1);
    std::memcpy(res.data(), &net_len, 4);
    res[4] = STATUS_OK;
    send(client_fd, res.data(), 5, 0);
}

void process_get(int client_fd, const std::vector<char>& buf, uint32_t offset) {
    uint16_t key_len;
    std::memcpy(&key_len, buf.data() + offset, 2);
    key_len = ntohs(key_len);
    offset += 2;
    std::string key(buf.data() + offset, key_len);

    std::string value;
    if (db.get(key, value)) {
        uint32_t res_len = 1 + 4 + value.size();
        std::vector<char> res(4 + res_len); 
        
        uint32_t net_len = htonl(res_len);
        std::memcpy(res.data(), &net_len, 4);
        res[4] = STATUS_OK;
        
        uint32_t vlen = htonl(value.size());
        std::memcpy(res.data() + 5, &vlen, 4);
        std::memcpy(res.data() + 9, value.data(), value.size());
        
        send(client_fd, res.data(), res.size(), 0);
    } else {
        // cout << "key not found: " << key << "\n";
        std::vector<char> res(5);
        uint32_t net_len = htonl(1);
        std::memcpy(res.data(), &net_len, 4);
        res[4] = STATUS_NOT_FOUND;
        send(client_fd, res.data(), 5, 0);
    }
}

// handles batch requests
void process_get_context(int client_fd, const std::vector<char>& buf, uint32_t offset) {
    uint16_t num_keys;
    std::memcpy(&num_keys, buf.data() + offset, 2);
    num_keys = ntohs(num_keys);
    offset += 2;

    std::vector<std::string> results;
    for(int i = 0; i < num_keys; ++i) {
        uint16_t klen;
        std::memcpy(&klen, buf.data() + offset, 2);
        klen = ntohs(klen);
        offset += 2;
        std::string k(buf.data() + offset, klen);
        offset += klen;
        
        std::string v;
        db.get(k, v); 
        results.push_back(std::move(v));
    }

    uint32_t res_len = 1 + 2; 
    for(const auto& v : results) res_len += 4 + v.size();

    std::vector<char> res(4 + res_len);
    uint32_t net_res_len = htonl(res_len);
    std::memcpy(res.data(), &net_res_len, 4);
    res[4] = STATUS_OK;
    
    uint16_t net_num = htons(num_keys);
    std::memcpy(res.data() + 5, &net_num, 2);
    
    uint32_t w_off = 7;
    for(const auto& v : results) {
        uint32_t vlen = htonl(v.size());
        std::memcpy(res.data() + w_off, &vlen, 4);
        w_off += 4;
        std::memcpy(res.data() + w_off, v.data(), v.size());
        w_off += v.size();
    }
    send(client_fd, res.data(), res.size(), 0);
}

void handle_client(int client_fd) {
    while (true) {
        uint32_t net_len = 0;
        ssize_t b = recv(client_fd, &net_len, 4, MSG_PEEK);
        if (b < 4) {
            if (b == 0) close(client_fd);
            break;
        }

        uint32_t total_len = ntohl(net_len);
        
        // cout << "incoming packet len: " << total_len << "\n";
        
        std::vector<char> buf(total_len + 4);
        b = recv(client_fd, buf.data(), buf.size(), MSG_WAITALL);
        if (b <= 0) break;

        uint32_t offset = 4;
        uint8_t opcode = buf[offset++];

        if (opcode == OP_SET) {
            process_set(client_fd, buf, offset);
        } else if (opcode == OP_GET) {
            process_get(client_fd, buf, offset);
        } else if (opcode == OP_GET_CONTEXT) {
            process_get_context(client_fd, buf, offset);
        }
    }
}

int main() {
    std::cout << "starting titandb..." << std::endl;
    // std::cout << "running on port 8080" << std::endl;
    
    EpollServer server(8080, handle_client);
    server.start();
    return 0;
}
