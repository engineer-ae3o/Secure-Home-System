#include "unity.h"

#include "file.hpp"
#include "utils.hpp"
#include "csprng.hpp"

#include <array>
#include <ranges>
#include <cstring>
#include <algorithm>

namespace rnd_test {

    void uninit_guards() {
        std::array<uint8_t, 16> buf{};

        auto ret = rnd::get_random_numbers(buf);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        ret = rnd::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void init() {
        auto ret = rnd::init();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double init must fail
        ret = rnd::init();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void deinit() {
        auto ret = rnd::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double deinit must fail
        ret = rnd::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void get_random_numbers_zero_span() {
        // Empty span; nothing to fill, should fail
        auto ret = rnd::get_random_numbers({});
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);
    }

    void get_random_numbers_single_byte() {
        uint8_t byte{};
        auto    ret = rnd::get_random_numbers({&byte, 1});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
        // No content assertion on one byte — too small to be meaningful
    }

    void get_random_numbers_standard_buffer() {
        std::array<uint8_t, 32> buf{};
        auto                    ret = rnd::get_random_numbers(buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void get_random_numbers_large_buffer() {
        std::array<uint8_t, 256> buf{};
        auto                     ret = rnd::get_random_numbers(buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    // Test output quality
    //
    // These are statistical sanity checks, not cryptographic proofs.
    // With a 32-byte buffer the probability of a false failure on any
    // individual assertion is negligible (< 2^-256 for the collision test).

    void output_not_all_zeros() {
        std::array<uint8_t, 32> buf{};
        auto                    ret = rnd::get_random_numbers(buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        const bool all_zero = std::ranges::all_of(buf, [](uint8_t b) {
            return b == 0;
        });
        TEST_ASSERT_FALSE(all_zero);
    }

    void output_not_all_identical_bytes() {
        std::array<uint8_t, 32> buf{};
        auto                    ret = rnd::get_random_numbers(buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // All 32 bytes being the same value is astronomically unlikely from a CSPRNG
        const bool all_same = std::all_of(buf.begin(), buf.end(), [&](uint8_t b) {
            return b == buf[0];
        });
        TEST_ASSERT_FALSE(all_same);
    }

    void successive_calls_differ() {
        // Two back-to-back calls should produce different output.
        // ASCON advances its state on each fetch so this must hold.
        std::array<uint8_t, 32> buf_a{};
        std::array<uint8_t, 32> buf_b{};

        auto ret = rnd::get_random_numbers(buf_a);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        ret = rnd::get_random_numbers(buf_b);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        TEST_ASSERT_NOT_EQUAL(0, memcmp(buf_a.data(), buf_b.data(), buf_a.size()));
    }

    void output_has_byte_diversity() {
        // Verify the output isn't stuck in a narrow range of values.
        // With 64 bytes from a healthy CSPRNG, we expect at least 20 distinct
        // byte values. A stuck/broken RNG would fail this convincingly.
        std::array<uint8_t, 64> buf{};
        auto                    ret = rnd::get_random_numbers(buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        bool seen[256]{};
        for (const auto b : buf) {
            seen[b] = true;
        }

        uint32_t distinct{};
        for (const auto s : seen) {
            distinct += s ? 1 : 0;
        }

        TEST_ASSERT_GREATER_THAN(20U, distinct);
    }

    void multiple_sequential_calls_succeed() {
        // Hammer the API to make sure the mutex and ASCON state hold up
        std::array<uint8_t, 16> buf{};
        for (uint8_t i{}; i < 20; i++) {
            auto ret = rnd::get_random_numbers(buf);
            TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
        }
    }

    void post_deinit_guards() {
        std::array<uint8_t, 16> buf{};

        auto ret = rnd::get_random_numbers(buf);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        ret = rnd::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void all() {
        // file must be up before rnd::init() since rnd reads the boot cycle count
        auto ret = file::init();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        RUN_TEST(uninit_guards);
        RUN_TEST(init);

        // Quality and edge-case tests, run while initialized
        RUN_TEST(get_random_numbers_zero_span);
        RUN_TEST(get_random_numbers_single_byte);
        RUN_TEST(get_random_numbers_standard_buffer);
        RUN_TEST(get_random_numbers_large_buffer);
        RUN_TEST(output_not_all_zeros);
        RUN_TEST(output_not_all_identical_bytes);
        RUN_TEST(successive_calls_differ);
        RUN_TEST(output_has_byte_diversity);
        RUN_TEST(multiple_sequential_calls_succeed);

        RUN_TEST(deinit);
        RUN_TEST(post_deinit_guards);

        ret = file::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

} // namespace rnd_test
