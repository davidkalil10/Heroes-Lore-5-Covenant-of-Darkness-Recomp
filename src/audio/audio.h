#pragma once
#include <cstdint>
#include <string>
namespace Audio {
bool init();
void shutdown();
int load(const std::string& vfs, uint64_t key, uint64_t offset, uint64_t length, bool music);
int play(uint64_t key, bool loop);
int stop(uint64_t key);
int pause(uint64_t key);
int resume(uint64_t key);
int free(uint64_t key);
void volume(bool music, int value);
int volume(bool music);
}
