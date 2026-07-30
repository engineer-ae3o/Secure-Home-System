#pragma once

#include "SEGGER_RTT.h"

#include "FreeRTOS.h"
#include "portmacro.h"
#include "projdefs.h"
#include "semphr.h"
#include "task.h"

#include <cstdint>
#include <utility>

namespace log {

    namespace {

        SemaphoreHandle_t g_log_mutex{};
        StaticSemaphore_t g_log_mutex_buffer{};

        struct scoped_mutex_t {
        public:
            scoped_mutex_t() {
                xSemaphoreTake(g_log_mutex, pdMS_TO_TICKS(portMAX_DELAY));
            }

            ~scoped_mutex_t() {
                xSemaphoreGive(g_log_mutex);
            }

            scoped_mutex_t(const scoped_mutex_t&)            = delete;
            scoped_mutex_t& operator=(const scoped_mutex_t&) = delete;
            scoped_mutex_t(scoped_mutex_t&&)                 = delete;
            scoped_mutex_t& operator=(scoped_mutex_t&&)      = delete;
        };

    } // namespace

    void init() {
        g_log_mutex = xSemaphoreCreateMutexStatic(&g_log_mutex_buffer);
    }

    void deinit() {
        if (g_log_mutex) {
            vSemaphoreDelete(g_log_mutex);
            g_log_mutex = nullptr;
        }
    }

    enum class level_t : uint8_t { INFO, WARN, ERROR };

    constexpr inline level_t SYSTEM_LOG_LEVEL = level_t::INFO;

    template<level_t level, typename... Args>
    void log(const char* tag, const char* fmt, Args&&... args) {
        // Filter based on the system log level
        if constexpr (std::to_underlying(level) >= std::to_underlying(SYSTEM_LOG_LEVEL)) {
            // Acquire RAII lock
            [[maybe_unused]] scoped_mutex_t mutex;

            // Set the output color
            if constexpr (level == level_t::INFO) {
                SEGGER_RTT_WriteString(0, RTT_CTRL_TEXT_GREEN);
            } else if constexpr (level == level_t::WARN) {
                SEGGER_RTT_WriteString(0, RTT_CTRL_TEXT_YELLOW);
            } else if constexpr (level == level_t::ERROR) {
                SEGGER_RTT_WriteString(0, RTT_CTRL_TEXT_RED);
            }

            // Get time stamp of the caller and write tag.
            const uint32_t time_stamp_ms = pdTICKS_TO_MS(xTaskGetTickCount());
            SEGGER_RTT_printf(0, "(%lums) [%s]: ", time_stamp_ms, tag);

            // Write the actual message
            SEGGER_RTT_printf(0, fmt, std::forward<Args>(args)...);

            // End line and clear the used color
            SEGGER_RTT_WriteString(0, "\r\n" RTT_CTRL_RESET);
        }
    }

} // namespace log
