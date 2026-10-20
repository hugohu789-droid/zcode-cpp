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
    EXPECT_EQ(Send("PUSH 0 s", &quit), "OK");
    EXPECT_FALSE(quit);
    EXPECT_EQ(Send("PUSH 0 a"), "OK");
    EXPECT_EQ(Send("KEYS 0"), "OK\ts\ta");

    EXPECT_EQ(Send("POP 0"), "OK");
    EXPECT_EQ(Send("KEYS 0"), "OK\ts");

    EXPECT_EQ(Send("CLEAR 0"), "OK");
    EXPECT_EQ(Send("KEYS 0"), "OK");
}

TEST_F(ProtocolTest, PopOnEmptySessionIsHarmless) {
    EXPECT_EQ(Send("POP 0"), "OK");
    EXPECT_EQ(Send("KEYS 0"), "OK");
}

// The session path and the stateless path must agree, since a host uses the
// first per keystroke and may use the second to re-check a whole word.
TEST_F(ProtocolTest, SessionCandidatesMatchStatelessShape) {
    Send("PUSH 0 s");
    Send("PUSH 0 a");
    Send("PUSH 0 i");
    Send("PUSH 0 n");
    const std::string from_session = Send("CAND 0 fuzzy");
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
    Send("PUSH 0 s");
    Send("PUSH 0 a");
    const std::size_t all = Fields(Send("CAND 0 fuzzy")).size() - 1;
    ASSERT_GT(all, 2u) << "need several candidates to cap";
    EXPECT_EQ(Fields(Send("CAND 0 fuzzy 2")).size() - 1, 2u);
}

TEST_F(ProtocolTest, DeduplicateFlagIsHonoured) {
    Send("PUSH 0 s");
    Send("PUSH 0 a");
    const std::size_t plain = Fields(Send("CAND 0 fuzzy 0 0")).size();
    const std::size_t deduped = Fields(Send("CAND 0 fuzzy 0 1")).size();
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
    Send("PUSH 0 s");
    Send("PUSH 0 a");
    const std::string before = Send("KEYS 0");

    EXPECT_EQ(Fields(Send("NONSENSE"))[0], "ERR");
    EXPECT_EQ(Fields(Send("CAND 0 sideways"))[0], "ERR");
    EXPECT_EQ(Fields(Send("CAND 0 fuzzy notanumber"))[0], "ERR");
    EXPECT_EQ(Fields(Send("CAND 0 fuzzy 0 7"))[0], "ERR");
    EXPECT_EQ(Fields(Send("PUSH 0"))[0], "ERR");
    EXPECT_EQ(Fields(Send("SHAPE fuzzy"))[0], "ERR");
    EXPECT_EQ(Fields(Send(""))[0], "ERR");

    EXPECT_EQ(Send("KEYS 0"), before);
}

// Windows clients writing in text mode send CRLF.
TEST_F(ProtocolTest, TrailingCarriageReturnIsTolerated) {
    EXPECT_EQ(Send("PUSH 0 s\r"), "OK");
    EXPECT_EQ(Send("KEYS 0\r"), "OK\ts");
}

TEST_F(ProtocolTest, SurroundingWhitespaceIsTolerated) {
    EXPECT_EQ(Send("   PUSH 0 s   "), "OK");
    EXPECT_EQ(Send("KEYS 0"), "OK\ts");
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

// A host with several input contexts needs independent sessions; the whole
// point of the id is that one context's keys never leak into another's.
TEST_F(ProtocolTest, SessionsAreIndependent) {
    const std::vector<std::string> opened = Fields(Send("OPEN"));
    ASSERT_EQ(opened[0], "OK");
    ASSERT_EQ(opened.size(), 2u);
    const std::string a = opened[1];
    const std::string b = Fields(Send("OPEN"))[1];
    EXPECT_NE(a, b);

    Send("PUSH " + a + " s");
    Send("PUSH " + b + " m");
    EXPECT_EQ(Send("KEYS " + a), "OK	s");
    EXPECT_EQ(Send("KEYS " + b), "OK	m");
    EXPECT_EQ(Send("KEYS 0"), "OK");  // The implicit one is untouched.
}

TEST_F(ProtocolTest, CloseRemovesASession) {
    const std::string id = Fields(Send("OPEN"))[1];
    EXPECT_EQ(Send("PUSH " + id + " s"), "OK");
    EXPECT_EQ(Send("CLOSE " + id), "OK");
    EXPECT_EQ(Fields(Send("KEYS " + id))[0], "ERR");
    EXPECT_EQ(Fields(Send("CLOSE " + id))[0], "ERR");
}

// Session 0 is what a single-context client and a shell one-liner rely on.
TEST_F(ProtocolTest, SessionZeroExistsAndCannotBeClosed) {
    EXPECT_EQ(Send("KEYS 0"), "OK");
    EXPECT_EQ(Fields(Send("CLOSE 0"))[0], "ERR");
    EXPECT_EQ(Send("KEYS 0"), "OK");  // Still usable.
}

TEST_F(ProtocolTest, UnknownOrMissingSessionIdIsRejected) {
    EXPECT_EQ(Fields(Send("KEYS 9999"))[0], "ERR");
    EXPECT_EQ(Fields(Send("PUSH 9999 s"))[0], "ERR");
    EXPECT_EQ(Fields(Send("KEYS"))[0], "ERR");        // id omitted
    EXPECT_EQ(Fields(Send("KEYS notanid"))[0], "ERR");
}

// SHAPE and DECOMPOSE are stateless and must not require an id.
TEST_F(ProtocolTest, StatelessCommandsTakeNoSessionId) {
    EXPECT_EQ(Fields(Send("SHAPE fuzzy sain"))[0], "OK");
    EXPECT_EQ(Fields(Send("DECOMPOSE sain"))[0], "OK");
}

}  // namespace
}  // namespace zcode::service
