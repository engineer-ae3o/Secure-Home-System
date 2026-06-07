extern "C" {
#include "unity.h"
}

#include "random.hpp"
#include "file.hpp"
#include "utils.hpp"

#include <array>
#include <algorithm>
#include <cstring>

// NOTE: rnd::init() calls file::get_boot_cycle_count() internally.
//       file::init() MUST be called before rnd::init(), and file::deinit()
//       must be called after rnd::deinit(). The test_all() runner handles this.

namespace rnd_test {

    // -------------------------------------------------------------------------
    // Uninit guards — all public functions must reject calls before init
    // -------------------------------------------------------------------------

    void test_uninit_guards() {
        std::array<uint8_t, 16> buf{};

        auto ret = rnd::get_random_numbers(buf);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        ret = rnd::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    // -------------------------------------------------------------------------
    // Init
    // -------------------------------------------------------------------------

    void test_init() {
        auto ret = rnd::init();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double init must fail
        ret = rnd::init();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    // -------------------------------------------------------------------------
    // Deinit
    // -------------------------------------------------------------------------

    void test_deinit() {
        auto ret = rnd::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double deinit must fail
        ret = rnd::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    // -------------------------------------------------------------------------
    // get_random_numbers: edge cases
    // -------------------------------------------------------------------------

    void test_get_random_numbers_zero_span() {
        // Empty span — nothing to fill, should succeed without touching memory
        auto ret = rnd::get_random_numbers({});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void test_get_random_numbers_single_byte() {
        uint8_t byte{};
        auto    ret = rnd::get_random_numbers({&byte, 1});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
        // No content assertion on one byte — too small to be meaningful
    }

    void test_get_random_numbers_standard_buffer() {
        std::array<uint8_t, 32> buf{};
        auto                    ret = rnd::get_random_numbers(buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void test_get_random_numbers_large_buffer() {
        std::array<uint8_t, 256> buf{};
        auto                     ret = rnd::get_random_numbers(buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    // -------------------------------------------------------------------------
    // Output quality
    //
    // These are statistical sanity checks, not cryptographic proofs.
    // With a 32-byte buffer the probability of a false failure on any
    // individual assertion is negligible (< 2^-256 for the collision test).
    // -------------------------------------------------------------------------

    void test_output_not_all_zeros() {
        std::array<uint8_t, 32> buf{};
        auto                    ret = rnd::get_random_numbers(buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        const bool all_zero = std::all_of(buf.begin(), buf.end(), [](uint8_t b) { return b == 0; });
        TEST_ASSERT_FALSE(all_zero);
    }

    void test_output_not_all_identical_bytes() {
        std::array<uint8_t, 32> buf{};
        auto                    ret = rnd::get_random_numbers(buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // All 32 bytes being the same value is astronomically unlikely from a CSPRNG
        const bool all_same = std::all_of(buf.begin(), buf.end(), [&](uint8_t b) { return b == buf[0]; });
        TEST_ASSERT_FALSE(all_same);
    }

    void test_successive_calls_differ() {
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

    void test_output_has_byte_diversity() {
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

    void test_multiple_sequential_calls_succeed() {
        // Hammer the API to make sure the mutex and ASCON state hold up
        std::array<uint8_t, 16> buf{};
        for (uint8_t i{}; i < 20; i++) {
            auto ret = rnd::get_random_numbers(buf);
            TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
        }
    }

    // -------------------------------------------------------------------------
    // Post-deinit guards (mirrors test_uninit_guards but after a full lifecycle)
    // -------------------------------------------------------------------------

    void test_post_deinit_guards() {
        std::array<uint8_t, 16> buf{};

        auto ret = rnd::get_random_numbers(buf);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        ret = rnd::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    // -------------------------------------------------------------------------
    // Runner — manages file + rnd lifecycle
    // -------------------------------------------------------------------------

    void test_all() {
        // file must be up before rnd::init() since rnd reads the boot cycle count
        auto ret = file::init();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        RUN_TEST(test_uninit_guards);
        RUN_TEST(test_init);

        // Quality and edge-case tests — run while initialized
        RUN_TEST(test_get_random_numbers_zero_span);
        RUN_TEST(test_get_random_numbers_single_byte);
        RUN_TEST(test_get_random_numbers_standard_buffer);
        RUN_TEST(test_get_random_numbers_large_buffer);
        RUN_TEST(test_output_not_all_zeros);
        RUN_TEST(test_output_not_all_identical_bytes);
        RUN_TEST(test_successive_calls_differ);
        RUN_TEST(test_output_has_byte_diversity);
        RUN_TEST(test_multiple_sequential_calls_succeed);

        RUN_TEST(test_deinit);
        RUN_TEST(test_post_deinit_guards);

        ret = file::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

} // namespace rnd_test
