/*******************************************************************************
 * This file is part of KaHyPar.
 *
 * Copyright (C) 2020 Lars Gottesbüren <lars.gottesbueren@kit.edu>
 *
 * KaHyPar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * KaHyPar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with KaHyPar.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/
#pragma once

#include <array>
#include <cassert>
#include <random>
#include <type_traits>


namespace mt_kahypar::hashing {

namespace integer {

// from parlay

inline uint32_t hash32(uint32_t a) {
  a = (a + 0x7ed55d16) + (a << 12);
  a = (a ^ 0xc761c23c) ^ (a >> 19);
  a = (a + 0x165667b1) + (a << 5);
  a = (a + 0xd3a2646c) ^ (a << 9);
  a = (a + 0xfd7046c5) + (a << 3);
  a = (a ^ 0xb55a4f09) ^ (a >> 16);
  return a;
}

inline uint32_t hash32_2(uint32_t a) {
  uint32_t z = (a + 0x6D2B79F5UL);
  z = (z ^ (z >> 15)) * (z | 1UL);
  z ^= z + (z ^ (z >> 7)) * (z | 61UL);
  return z ^ (z >> 14);
}

inline uint32_t hash32_3(uint32_t a) {
  uint32_t z = a + 0x9e3779b9;
  z ^= z >> 15;
  z *= 0x85ebca6b;
  z ^= z >> 13;
  z *= 0xc2b2ae3d;  // 0xc2b2ae35 for murmur3
  return z ^= z >> 16;
}

inline uint64_t hash64(uint64_t u) {
  uint64_t v = u * 3935559000370003845ul + 2691343689449507681ul;
  v ^= v >> 21;
  v ^= v << 37;
  v ^= v >> 4;
  v *= 4768777513237032717ul;
  v ^= v << 20;
  v ^= v >> 41;
  v ^= v << 5;
  return v;
}

inline uint64_t hash64_2(uint64_t x) {
  x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
  x = x ^ (x >> 31);
  return x;
}

// from boost::hash_combine
inline uint32_t combine(uint32_t left, uint32_t hashed_right) {
  return left ^ (hashed_right + 0x9e3779b9 + (left << 6) + (left >> 2));
}

inline uint32_t combine2(uint32_t left, uint32_t hashed_right) {
  constexpr uint32_t c1 = 0xcc9e2d51;
  constexpr uint32_t c2 = 0x1b873593;
  constexpr auto rotate_left = [](uint32_t x, uint32_t r) -> uint32_t {
    return (x << r) | (x >> (32 - r));
  };

  hashed_right *= c1;
  hashed_right = rotate_left(hashed_right,15);
  hashed_right *= c2;

  left ^= hashed_right;
  left = rotate_left(left,13);
  left = left * 5 + 0xe6546b64;
  return left;
}

} // namespace integer


// Lorenz' tabulation hashing implementation from thrill. TODO figure out licensing?

  /*!
 * Tabulation Hashing, see https://en.wikipedia.org/wiki/Tabulation_hashing
 *
 * Keeps a table with size * 256 entries of type hash_t, filled with random
 * values.  Elements are hashed by treating them as a vector of 'size' bytes,
 * and XOR'ing the values in the data[i]-th position of the i-th table, with i
 * ranging from 0 to size - 1.
 */

template <size_t size, typename hash_t = uint32_t, typename prng_t = std::mt19937>
class TabulationHashing
{
public:
  using hash_type = hash_t;  // make public
  using prng_type = prng_t;
  using Subtable = std::array<hash_type, 256>;
  using Table = std::array<Subtable, size>;

  explicit TabulationHashing(size_t seed = 0) { init(seed); }

  //! (re-)initialize the table by filling it with random values
  void init(const size_t seed) {
    prng_t rng { seed };
    for (size_t i = 0; i < size; ++i) {
      for (size_t j = 0; j < 256; ++j) {
        table_[i][j] = rng();
      }
    }
  }

  //! Hash an element
  template <typename T>
  hash_type operator () (const T& x) const {
    static_assert(sizeof(T) == size, "Size mismatch with operand type");

    hash_t hash = 0;
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&x);
    for (size_t i = 0; i < size; ++i) {
      hash ^= table_[i][*(ptr + i)];
    }
    return hash;
  }

protected:
  Table table_;
};

//! Tabulation hashing
template <typename T, typename hash_t = uint32_t>
using HashTabulated = TabulationHashing<sizeof(T), hash_t>;

template <typename ValueType, size_t bits = 32, typename Hash = HashTabulated>
struct masked_hash {
  static constexpr size_t Bits = bits;  // export
  using hash_t = decltype(std::declval<Hash>()(std::declval<ValueType>()));

  static_assert(bits <= 8 * sizeof(hash_t), "Not enough bits in Hash");
  static constexpr hash_t mask = static_cast<hash_t>((1UL << bits) - 1);

  inline hash_t operator () (const ValueType& val) const {
    return hash(val) & mask;
  }

private:
  Hash hash;
};

template<typename T>
struct SimpleIntHash {
  using hash_type = T;

  void init(T /* seed */) {
    // intentionally unimplemented
  }

  T operator()(T x) {
    if constexpr (sizeof(T) == 4) {
      return integer::hash32(x);
    } else if constexpr (sizeof(T) == 8) {
      return integer::hash64(x);
    } else {
      return x;
    }
  }
};


// implements the rng interface required for std::uniform_int_distribution
template<typename Hash>
struct HashRNG {
  using result_type = uint32_t;
  static_assert(std::is_same<result_type, Hash::hash_type>::value);   // we don't have 64 bit combine

  explicit HashRNG(result_type seed) {
    hash.init(seed);
    init(seed);
  }

  // allow reinit. with tabulation hashing we don't want to recompute the full table
  // since we want to reinit on every new neighbor
  void init(result_type seed) {
    state = seed;
  }

  constexpr result_type min() { return std::numeric_limits<result_type>::min(); }
  constexpr result_type max() { return std::numeric_limits<result_type>::max(); }

  // don't do too many calls of this without calls to init
  result_type operator()() {
    state = hash(state);
    return state;
  }

private:
  result_type state;
  Hash hash;
};

using SimpleHashRNG = HashRNG<SimpleIntHash<uint32_t>>;
using TabulationHashRNG = HashRNG<HashTabulated<uint32_t, uint32_t>>;

} // namespace mt_kahypar::hashing