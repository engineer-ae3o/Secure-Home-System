#include "unity.h"

#include "utils.hpp"
#include "sim800l.hpp"

#include <array>
#include <cstring>
#include <string_view>

namespace gsm::test {

    namespace {

        // Valid fixtures
        constexpr std::string_view VALID_NUMBER = "08052879413"; // 11 chars, exactly MAX_PHONE_NUMBER_LEN
        constexpr std::string_view SHORT_NUMBER = "0801234";     // 7 chars, well within limit
        constexpr std::string_view VALID_SMS    = "Test message";
        constexpr std::string_view EMPTY_SMS{};

        // Boundary violation fixtures: never reach the radio
        constexpr std::string_view SMS_TOO_LONG =
            "This SMS is intentionally longer than the 255 character maximum limit. Still going. You're still here?"
            "Go away. What are you still doing here? Man you're a buzz kill. Go on, get off and go do something useful"
            "with yer life for once. Go make mama proud. Get lost man. Finally, it ends";

        static_assert(SMS_TOO_LONG.size() > MAX_SMS_LEN);
        static_assert(VALID_NUMBER.size() == MAX_PHONE_NUMBER_LEN);

        // One character over the phone number limit
        constexpr std::string_view NUMBER_TOO_LONG = "080123456789"; // 12 chars

        static_assert(NUMBER_TOO_LONG.size() > MAX_PHONE_NUMBER_LEN);

    } // namespace

    // SECTION 1: State machine tests. Doesn't require the SIM800L.

    void test_uninit_guards() {
        auto ret = get_sim_status();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        ret = send_sms(VALID_SMS, VALID_NUMBER);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        ret = deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        auto imsi = get_imsi();
        TEST_ASSERT_FALSE(imsi.has_value());
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, imsi.error());
    }

    void test_send_sms_arg_validation() {
        // SMS too long
        auto ret = send_sms(SMS_TOO_LONG, VALID_NUMBER, false);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);

        // Number too long
        ret = send_sms(VALID_SMS, NUMBER_TOO_LONG, false);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);

        // Both too long: arg check fires on SMS first since it's checked first
        ret = send_sms(SMS_TOO_LONG, NUMBER_TOO_LONG, false);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);

        // Exactly at the limits: should NOT return ERR_INVALID_ARG
        // (may fail later at the hardware level, but not here)
        // We intentionally don't assert NONE here since the radio may or
        // may not succeed: we just assert it didn't fail on arg checking.
        ret = send_sms(VALID_SMS, VALID_NUMBER, false);
        TEST_ASSERT_NOT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);
    }

    void test_send_sms_empty_sms_not_invalid_arg() {
        // Empty SMS is 0 bytes, which is <= MAX_SMS_LEN, so not an arg error.
        // Hardware may reject it but that's a different error code.
        auto ret = send_sms(EMPTY_SMS, VALID_NUMBER, false);
        TEST_ASSERT_NOT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);
    }

    // SECTION 2: Integration tests — require live SIM800L + registered SIM card

    void test_init() {
        auto ret = init();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double init must fail
        ret = init();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void test_get_sim_status() {
        auto ret = get_sim_status();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void test_get_sim_status_is_idempotent() {
        // Calling it multiple times should not corrupt state
        for (uint8_t i{}; i < 3; i++) {
            auto ret = get_sim_status();
            TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
        }
    }

    void test_get_imsi_returns_value() {
        auto result = get_imsi();
        TEST_ASSERT_TRUE(result.has_value());
    }

    void test_get_imsi_is_15_ascii_digits() {
        auto result = get_imsi();
        TEST_ASSERT_TRUE(result.has_value());

        const auto& imsi = result.value();

        // First 15 bytes must all be ASCII digit characters ('0'–'9')
        for (uint8_t i{}; i < 15; i++) {
            TEST_ASSERT_TRUE(imsi[i] >= '0' && imsi[i] <= '9');
        }
    }

    void test_get_imsi_is_consistent_across_calls() {
        // IMSI is a static property of the SIM card. The two reads must match
        auto result_a = get_imsi();
        auto result_b = get_imsi();

        TEST_ASSERT_TRUE(result_a.has_value());
        TEST_ASSERT_TRUE(result_b.has_value());

        TEST_ASSERT_EQUAL(0, memcmp(result_a->data(), result_b->data(), 15));
    }

    void test_send_sms_succeeds() {
        // Sends a real SMS. Requires a live network connection.
        auto ret = send_sms("STM32 test SMS", VALID_NUMBER);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void test_send_sms_without_status_check() {
        // check_sim_status = false skips get_sim_status() internally.
        // The SMS should still send successfully.
        auto ret = send_sms("No status check", VALID_NUMBER, false);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void test_send_sms_max_length_content() {
        // Build an SMS that is exactly MAX_SMS_LEN characters
        std::array<char, MAX_SMS_LEN + 1> sms{};
        sms.fill('A');

        auto ret = send_sms({sms.data(), sms.size()}, VALID_NUMBER);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void test_send_sms_short_number() {
        auto ret = send_sms("Short number test", SHORT_NUMBER);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void test_deinit() {
        auto ret = deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double deinit must fail
        ret = deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void test_post_deinit_guards() {
        auto ret = get_sim_status();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        ret = send_sms(VALID_SMS, VALID_NUMBER);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        auto imsi = get_imsi();
        TEST_ASSERT_FALSE(imsi.has_value());
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, imsi.error());
    }

    // Run only state machine tests. No SIM800L hardware required
    void test_state_machine_only() {
        RUN_TEST(test_uninit_guards);

        // Init so arg validation tests can fire
        auto ret = init();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        RUN_TEST(test_send_sms_arg_validation);
        RUN_TEST(test_send_sms_empty_sms_not_invalid_arg);

        ret = deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        RUN_TEST(test_post_deinit_guards);
    }

    // Full integration run requires live SIM800L + registered SIM card
    void all() {
        RUN_TEST(test_uninit_guards);
        RUN_TEST(test_init);

        RUN_TEST(test_send_sms_arg_validation);
        RUN_TEST(test_send_sms_empty_sms_not_invalid_arg);

        RUN_TEST(test_get_sim_status);
        RUN_TEST(test_get_sim_status_is_idempotent);

        RUN_TEST(test_get_imsi_returns_value);
        RUN_TEST(test_get_imsi_is_15_ascii_digits);
        RUN_TEST(test_get_imsi_is_consistent_across_calls);

        RUN_TEST(test_send_sms_succeeds);
        RUN_TEST(test_send_sms_without_status_check);
        RUN_TEST(test_send_sms_max_length_content);
        RUN_TEST(test_send_sms_short_number);

        RUN_TEST(test_deinit);
        RUN_TEST(test_post_deinit_guards);
    }

} // namespace gsm::test
