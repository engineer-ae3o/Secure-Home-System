namespace file {
    extern void test_all();
}

namespace lcd {
    extern void test_all();
}

namespace tests {
    // Run all tests
    void all() {
        file::test_all();
        lcd::test_all();
    }
} // namespace tests
