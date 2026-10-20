// SPDX-License-Identifier: GPL-2.0-or-later
//
// The stdio service's line protocol. Driven directly, without a process, so
// these are ordinary unit tests.

#include "../service/protocol.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "zcode/zcode.h"

namespace zcode::service {
namespace {

std::vector<std::string> Fields(const std::string& response) {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (true) {
        const std::size_t tab = response.find('\t', begin);
        if (tab == std::string::npos) {
            fields.push_back(response.substr(begin));
            break;
        }
        fields.push_back(response.substr(begin, tab - begin));
        begin = tab + 1;
    }
    return fields;
}

class ProtocolTest : public ::testing::Test {
protected:
    ProtocolTest() {
        std::string error;
        auto created = Engine::create(&error);
        EXPECT_TRUE(created.has_value()) << error;
        engine_ = std::move(created);
        protocol_ = std::make_unique<Protocol>(*engine_);
    }

    std::string Send(std::string_view line, bool* quit = nullptr) {
        return protocol_->handle(line, quit);
    }

    std::optional<Engine> engine_;
    std::unique_ptr<Protocol> protocol_;
};

TEST_F(ProtocolTest, QuitReportsAndAcknowledges) {
    bool quit = false;
    EXPECT_EQ(Send("QUIT", &quit), "OK");
    EXPECT_TRUE(quit);
}

TEST_F(ProtocolTest, PushPopAndKeysTrackTheSession) {
    bool quit = true;  // Must be cleared by a non-quit request.
    EXPECT_EQ(Send("PUSH s", &quit), "OK");
    EXPECT_FALSE(quit);
    EXPECT_EQ(Send("PUSH a"), "OK");
    EXPECT_EQ(Send("KEYS"), "OK\ts\ta");

    EXPECT_EQ(Send("POP"), "OK");
    EXPECT_EQ(Send("KEYS"), "OK\ts");

    EXPECT_EQ(Send("CLEAR"), "OK");
    EXPECT_EQ(Send("KEYS"), "OK");
}

TEST_F(ProtocolTest, PopOnEmptySessionIsHarmless) {
    EXPECT_EQ(Send("POP"), "OK");
    EXPECT_EQ(Send("KEYS"), "OK");
}

// The session path and the stateless path must agree, since a host uses the
// first per keystroke and may use the second to re-check a whole word.
TEST_F(ProtocolTest, SessionCandidatesMatchStatelessShape) {
    Send("PUSH s");
    Send("PUSH a");
    Send("PUSH i");
    Send("PUSH n");
    const std::string from_session = Send("CAND fuzzy");
    const std::string from_shape = Send("SHAPE fuzzy sain");
    EXPECT_EQ(from_session, from_shape);
    EXPECT_EQ(Fields(from_session)[0], "OK");
    EXPECT_GT(Fields(from_session).size(), 1u) << "expected candidates";
}

TEST_F(ProtocolTest, BothModesAreAccepted) {
    EXPECT_EQ(Fields(Send("SHAPE severe sain"))[0], "OK");
    EXPECT_EQ(Fields(Send("SHAPE fuzzy sain"))[0], "OK");
}

TEST_F(ProtocolTest, MaxCandidatesCapsTheList) {
    Send("PUSH s");
    Send("PUSH a");
    const std::size_t all = Fields(Send("CAND fuzzy")).size() - 1;
    ASSERT_GT(all, 2u) << "need several candidates to cap";
    EXPECT_EQ(Fields(Send("CAND fuzzy 2")).size() - 1, 2u);
}

TEST_F(ProtocolTest, DeduplicateFlagIsHonoured) {
    Send("PUSH s");
    Send("PUSH a");
    const std::size_t plain = Fields(Send("CAND fuzzy 0 0")).size();
    const std::size_t deduped = Fields(Send("CAND fuzzy 0 1")).size();
    EXPECT_LE(deduped, plain);
}

TEST_F(ProtocolTest, DecomposeReturnsSplitsAsSpaceJoinedKeys) {
    const std::vector<std::string> fields = Fields(Send("DECOMPOSE sain"));
    EXPECT_EQ(fields[0], "OK");
    ASSERT_GT(fields.size(), 1u);
    // Every split is a space-joined key list, never empty.
    for (std::size_t i = 1; i < fields.size(); ++i) {
        EXPECT_FALSE(fields[i].empty());
    }
}

// A rejected request must not disturb the session, so a client can recover.
TEST_F(ProtocolTest, ErrorsLeaveTheSessionIntact) {
    Send("PUSH s");
    Send("PUSH a");
    const std::string before = Send("KEYS");

    EXPECT_EQ(Fields(Send("NONSENSE"))[0], "ERR");
    EXPECT_EQ(Fields(Send("CAND sideways"))[0], "ERR");
    EXPECT_EQ(Fields(Send("CAND fuzzy notanumber"))[0], "ERR");
    EXPECT_EQ(Fields(Send("CAND fuzzy 0 7"))[0], "ERR");
    EXPECT_EQ(Fields(Send("PUSH"))[0], "ERR");
    EXPECT_EQ(Fields(Send("SHAPE fuzzy"))[0], "ERR");
    EXPECT_EQ(Fields(Send(""))[0], "ERR");

    EXPECT_EQ(Send("KEYS"), before);
}

// Windows clients writing in text mode send CRLF.
TEST_F(ProtocolTest, TrailingCarriageReturnIsTolerated) {
    EXPECT_EQ(Send("PUSH s\r"), "OK");
    EXPECT_EQ(Send("KEYS\r"), "OK\ts");
}

TEST_F(ProtocolTest, SurroundingWhitespaceIsTolerated) {
    EXPECT_EQ(Send("   PUSH s   "), "OK");
    EXPECT_EQ(Send("KEYS"), "OK\ts");
}

// ZCode output must never contain the field separator, or a client could not
// split the response. The code points are U+1865..U+18AA, so this holds; pin
// it, because the whole protocol depends on it.
TEST_F(ProtocolTest, CandidatesNeverContainTabOrNewline) {
    for (const char* latin : {"sain", "mongol", "bicig", "aimag"}) {
        const std::string response = Send(std::string("SHAPE fuzzy ") + latin);
        const std::vector<std::string> fields = Fields(response);
        ASSERT_EQ(fields[0], "OK");
        for (std::size_t i = 1; i < fields.size(); ++i) {
            EXPECT_EQ(fields[i].find('\n'), std::string::npos);
            EXPECT_EQ(fields[i].find('\r'), std::string::npos);
        }
    }
}

}  // namespace
}  // namespace zcode::service
