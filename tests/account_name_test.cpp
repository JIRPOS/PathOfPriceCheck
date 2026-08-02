#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "config.hpp"

using ppc::check_account_name;
using ppc::NameCheck;

TEST_CASE("empty is valid — the field is optional") {
    CHECK(check_account_name("") == NameCheck::Empty);
}

TEST_CASE("well-formed handles") {
    CHECK(check_account_name("Foo#1234") == NameCheck::Ok);
    CHECK(check_account_name("a1#0") == NameCheck::Ok);
    CHECK(check_account_name("Exile42#0001") == NameCheck::Ok);
}

TEST_CASE("malformed handles") {
    CHECK(check_account_name("Foo") == NameCheck::Malformed);        // no discriminator
    CHECK(check_account_name("#1234") == NameCheck::Malformed);      // no name
    CHECK(check_account_name("Foo#") == NameCheck::Malformed);       // no digits
    CHECK(check_account_name("Fo o#1") == NameCheck::Malformed);     // space in name
    CHECK(check_account_name("Foo_bar#1") == NameCheck::Malformed);  // underscore in name
    CHECK(check_account_name("Foo#12a") == NameCheck::Malformed);    // letter in digits
    CHECK(check_account_name("Foo#1#2") == NameCheck::Malformed);    // second '#'
    CHECK(check_account_name("Foo#-1") == NameCheck::Malformed);     // sign in digits
}

// The name half is alphanumeric only, so a non-ASCII byte must not slip through
// isalnum() via sign extension on a signed char.
TEST_CASE("high bytes are rejected, not passed to isalnum as negative") {
    CHECK(check_account_name("Fo\xc3\xb6#1") == NameCheck::Malformed);
}
