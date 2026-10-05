#pragma once
#include <cstdint>

constexpr uint8_t OP_GET = 0;
constexpr uint8_t OP_SET = 1;
constexpr uint8_t OP_GET_CONTEXT = 2; // AI Context Batching (RAG)

constexpr uint8_t STATUS_OK = 0;
constexpr uint8_t STATUS_NOT_FOUND = 1;
constexpr uint8_t STATUS_ERROR = 2;
