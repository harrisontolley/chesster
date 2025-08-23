#include <catch2/catch_test_macros.hpp>
#include "bitboard.hh"

using namespace engine;

TEST_CASE("Square helpers")
{
    SECTION("File extraction")
    {
        REQUIRE(file(A1) == 0);
        REQUIRE(file(B2) == 1);
        REQUIRE(file(C3) == 2);
        REQUIRE(file(D4) == 3);
        REQUIRE(file(E5) == 4);
        REQUIRE(file(F6) == 5);
        REQUIRE(file(G7) == 6);
        REQUIRE(file(H8) == 7);
    }

    SECTION("Rank extraction")
    {
        REQUIRE(rank(A1) == 0);
        REQUIRE(rank(A2) == 1);
        REQUIRE(rank(A3) == 2);
        REQUIRE(rank(A4) == 3);
        REQUIRE(rank(A5) == 4);
        REQUIRE(rank(A6) == 5);
        REQUIRE(rank(A7) == 6);
        REQUIRE(rank(A8) == 7);
    }
}

TEST_CASE("north/east masks")
{
    REQUIRE(north(0x1ULL) == 0x100ULL);
    REQUIRE(east(0x0101010101010101ULL) == 0x0202020202020202ULL);
}
