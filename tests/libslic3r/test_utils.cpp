#include <catch2/catch_test_macros.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"

#include <boost/log/trivial.hpp>

SCENARIO("Test fast_round_up()") {
    using namespace Slic3r;

    THEN("fast_round_up<int>(1.5) is 2") {
        REQUIRE(fast_round_up<int>(1.5) == 2);
    }
    THEN("fast_round_up<int>(1.499999999999999) is 1") {
        REQUIRE(fast_round_up<int>(1.499999999999999) == 1);
    }
    THEN("fast_round_up<int>(0.5) is 1") {
        REQUIRE(fast_round_up<int>(0.5) == 1);
    }
    THEN("fast_round_up<int>(0.49999999999999994) is 0") {
        REQUIRE(fast_round_up<int>(0.49999999999999994) == 0);
    }
    THEN("fast_round_up<int>(-0.5) is 0") {
        REQUIRE(fast_round_up<int>(-0.5) == 0);
    }
    THEN("fast_round_up<int>(-0.51) is -1") {
        REQUIRE(fast_round_up<int>(-0.51) == -1);
    }
    THEN("fast_round_up<int>(-0.51) is -1") {
        REQUIRE(fast_round_up<int>(-0.51) == -1);
    }
    THEN("fast_round_up<int>(-1.5) is -1") {
        REQUIRE(fast_round_up<int>(-1.5) == -1);
    }
    THEN("fast_round_up<int>(-1.51) is -2") {
        REQUIRE(fast_round_up<int>(-1.51) == -2);
    }
}

TEST_CASE("The last lines of the log are kept for the problem reports", "[utils]") {
    using namespace Slic3r;
    const unsigned level = get_logging_level();
    set_logging_level(1);
    keep_recent_log(3);
    BOOST_LOG_TRIVIAL(info) << "not kept: below the logging level";
    for (int i = 0; i < 5; ++ i)
        BOOST_LOG_TRIVIAL(error) << "problem " << i;
    const std::string log = recent_log();
    CHECK(log.find("not kept") == std::string::npos);
    CHECK(log.find("problem 1") == std::string::npos);
    CHECK(log.find("[error]   problem 2\n") != std::string::npos);
    CHECK(log.find("[error]   problem 4\n") != std::string::npos);
    set_logging_level(level);
}
