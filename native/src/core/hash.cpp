// Akeno Gaming Hub - SHA-256 and Ed25519 signature verification.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/hash.hpp"

#include "monocypher-ed25519.h"
#include "picosha2.h"

namespace akeno
{

struct Sha256::State
{
    picosha2::hash256_one_by_one hasher;
};

Sha256::Sha256() : state_(new State) { state_->hasher.init(); }
Sha256::~Sha256() = default;

void Sha256::update(const void *data, std::size_t size)
{
    const auto *bytes = static_cast<const unsigned char *>(data);
    state_->hasher.process(bytes, bytes + size);
}

std::string Sha256::hex()
{
    state_->hasher.finish();
    return picosha2::get_hash_hex_string(state_->hasher);
}

std::string sha256_hex(std::string_view data)
{
    Sha256 hash;
    hash.update(data);
    return hash.hex();
}

bool from_hex(std::string_view hex, std::uint8_t *out, std::size_t size)
{
    if (hex.size() != size * 2)
        return false;
    const auto value = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < size; ++i)
    {
        const int high = value(hex[2 * i]), low = value(hex[2 * i + 1]);
        if (high < 0 || low < 0)
            return false;
        out[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return true;
}

bool ed25519_verify(const std::uint8_t public_key[32], const std::uint8_t signature[64], std::string_view message)
{
    return crypto_ed25519_check(signature, public_key, reinterpret_cast<const std::uint8_t *>(message.data()),
                                message.size()) == 0;
}

bool digest_equal(std::string_view a, std::string_view b)
{
    if (a.size() != b.size() || a.empty())
        return false;
    unsigned difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'F')
            x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'F')
            y = static_cast<char>(y - 'A' + 'a');
        difference |= static_cast<unsigned>(x ^ y);
    }
    return difference == 0;
}

} // namespace akeno
