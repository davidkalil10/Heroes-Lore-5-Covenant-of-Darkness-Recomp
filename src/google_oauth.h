#pragma once
#include <string>
#include <cstdint>
namespace services {
std::string getGoogleClientId() {
  static const uint8_t enc[] = {
    108, 104, 108, 99, 104, 98, 111, 110, 98, 107, 111, 107, 119, 107, 104, 98, 59, 42, 50, 54, 59, 52, 46, 110, 104, 111, 54, 61, 109, 47, 57, 108, 55, 106, 43, 53, 44, 48, 47, 108, 52, 63, 60, 41, 61, 116, 59, 42, 42, 41, 116, 61, 53, 53, 61, 54, 63, 47, 41, 63, 40, 57, 53, 52, 46, 63, 52, 46, 116, 57, 53, 55
  };
  std::string s;
  s.reserve(sizeof(enc));
  for (uint8_t b : enc) s.push_back((char)(b ^ 0x5A));
  return s;
}

std::string getGoogleClientSecret() {
  static const uint8_t enc[] = {
    29, 21, 25, 9, 10, 2, 119, 45, 107, 28, 106, 56, 21, 48, 106, 99, 109, 107, 44, 11, 98, 119, 119, 61, 23, 24, 48, 43, 106, 8, 25, 109, 104, 45, 10
  };
  std::string s;
  s.reserve(sizeof(enc));
  for (uint8_t b : enc) s.push_back((char)(b ^ 0x5A));
  return s;
}


}
