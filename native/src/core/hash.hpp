// Akeno Gaming Hub - SHA-256 and Ed25519 signature verification.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace akeno
{

class Sha256
{
  public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256 &) = delete;
    Sha256 &operator=(const Sha256 &) = delete;

    void update(const void *data, std::size_t size);
    void update(std::string_view data) { update(data.data(), data.size()); }
    std::string hex();

  private:
    struct State;
    std::unique_ptr<State> state_;
};

std::string sha256_hex(std::string_view data);

// Hex helpers for keys and signatures; return false on malformed input.
bool from_hex(std::string_view hex, std::uint8_t *out, std::size_t size);

// Ed25519 (RFC 8032) verification of `message` with a 32-byte key and 64-byte signature.
bool ed25519_verify(const std::uint8_t public_key[32], const std::uint8_t signature[64], std::string_view message);

// Constant-time comparison of two equal-length hex strings, case-insensitive.
bool digest_equal(std::string_view a, std::string_view b);

} // namespace akeno
