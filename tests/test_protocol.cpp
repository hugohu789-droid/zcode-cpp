// SPDX-License-Identifier: GPL-2.0-or-later
//
// The stdio service's line protocol. Driven directly, without a process, so
// these are ordinary unit tests.

#include "../service/protocol.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "zcode/dictionary.h"
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

// A host checks the protocol version before trusting an engine binary, so a
// mismatched one fails at startup instead of on a real request.
TEST_F(ProtocolTest, HelloReportsNameAndVersion) {
    const std::vector<std::string> fields = Fields(Send("HELLO"));
    ASSERT_EQ(fields.size(), 3u);
    EXPECT_EQ(fields[0], "OK");
    EXPECT_EQ(fields[1], "paiza-engine");
    EXPECT_EQ(fields[2], "1");
}

// TYPE exists so a keystroke costs one round trip instead of two. It must be
// exactly PUSH followed by CAND, or a host would get different candidates
// depending on which it used.
TEST_F(ProtocolTest, TypeEqualsPushThenCand) {
    const std::string a = Fields(Send("OPEN"))[1];
    const std::string b = Fields(Send("OPEN"))[1];

    Send("PUSH " + a + " s");
    const std::string via_push = Send("CAND " + a + " fuzzy 5");
    const std::string via_type = Send("TYPE " + b + " s fuzzy 5");
    EXPECT_EQ(via_type, via_push);
}

TEST_F(ProtocolTest, UntypeEqualsPopThenCand) {
    const std::string a = Fields(Send("OPEN"))[1];
    const std::string b = Fields(Send("OPEN"))[1];
    for (const std::string& id : {a, b}) {
        Send("PUSH " + id + " s");
        Send("PUSH " + id + " a");
    }
    Send("POP " + a);
    const std::string via_pop = Send("CAND " + a + " fuzzy 5");
    const std::string via_untype = Send("UNTYPE " + b + " fuzzy 5");
    EXPECT_EQ(via_untype, via_pop);
    EXPECT_EQ(Send("KEYS " + b), "OK\ts");
}

// A control character in a field would break the line framing, so it is
// rejected rather than stripped: stripping turns an injection attempt into a
// valid-looking request.
TEST_F(ProtocolTest, ControlCharactersInKeysAreRejected) {
    EXPECT_EQ(Fields(Send("PUSH 0 a\tb"))[0], "ERR");
    EXPECT_EQ(Fields(Send("TYPE 0 a\tb fuzzy"))[0], "ERR");
    EXPECT_EQ(Send("KEYS 0"), "OK");  // Nothing was pushed.
}

TEST_F(ProtocolTest, PushTakesExactlyOneKey) {
    EXPECT_EQ(Fields(Send("PUSH 0 s a"))[0], "ERR");
    EXPECT_EQ(Send("KEYS 0"), "OK");
}

TEST_F(ProtocolTest, TypeAndUntypeNeedAValidSessionAndMode) {
    EXPECT_EQ(Fields(Send("TYPE 9999 s fuzzy"))[0], "ERR");
    EXPECT_EQ(Fields(Send("TYPE 0 s sideways"))[0], "ERR");
    EXPECT_EQ(Fields(Send("TYPE 0"))[0], "ERR");
    EXPECT_EQ(Fields(Send("UNTYPE 9999 fuzzy"))[0], "ERR");
    EXPECT_EQ(Send("KEYS 0"), "OK");
}

// UNTYPE on an empty session is the backspace-past-the-start case.
TEST_F(ProtocolTest, UntypeOnEmptySessionIsHarmless) {
    EXPECT_EQ(Send("UNTYPE 0 fuzzy"), "OK");
    EXPECT_EQ(Send("KEYS 0"), "OK");
}

// ---- the dictionary modes ------------------------------------------------
//
// A session query may ask for dictionary words instead of letter shapes. This
// build has a dictionary, so a second fixture gives the protocol one; the
// fixture above deliberately has none, which is how the rejection path above
// gets tested.

class ProtocolDictionaryTest : public ::testing::Test {
protected:
    ProtocolDictionaryTest() {
        std::string error;
        auto created = Engine::create(&error);
        EXPECT_TRUE(created.has_value()) << error;
        engine_ = std::move(created);
        auto dictionary = Dictionary::create(&error);
        EXPECT_TRUE(dictionary.has_value()) << error;
        dictionary_ = std::move(dictionary);
        protocol_ = std::make_unique<Protocol>(*engine_, &dictionary_.value());
    }

    std::string Send(std::string_view line, bool* miss = nullptr) {
        return protocol_->handle(line, nullptr, miss);
    }

    // Types `latin` into an empty session one key at a time and returns the
    // last answer, which is how a host drives this: one TYPE per keystroke.
    std::string Type(std::string_view latin, std::string_view mode,
                     std::string_view args = "") {
        std::string response = Send("CLEAR 0");
        for (const char c : latin) {
            std::string request = "TYPE 0 ";
            request += c;
            request += ' ';
            request.append(mode);
            if (!args.empty()) {
                request += ' ';
                request.append(args);
            }
            response = Send(request);
        }
        return response;
    }

    std::optional<Engine> engine_;
    std::optional<Dictionary> dictionary_;
    std::unique_ptr<Protocol> protocol_;
};

// The point of the whole exercise: a host that asks for "score" gets the
// words with every metric behind the ranking, so it can rank them against
// its own sources instead of trusting the order.
TEST_F(ProtocolDictionaryTest, ScoreModeCarriesEveryMetric) {
    const std::vector<std::string> fields = Fields(Type("bagsi", "score", "9"));
    ASSERT_EQ(fields[0], "OK");
    ASSERT_GT(fields.size(), 1U);
    for (std::size_t i = 1; i < fields.size(); ++i) {
        const std::string& item = fields[i];
        EXPECT_NE(item.find(",frequency:"), std::string::npos) << item;
        EXPECT_NE(item.find(",frequencyScore:"), std::string::npos) << item;
        EXPECT_NE(item.find(",levenshteinD:"), std::string::npos) << item;
        EXPECT_NE(item.find(",levenshteinScore:"), std::string::npos) << item;
        EXPECT_NE(item.find(",jaroWinklerScore:"), std::string::npos) << item;
        EXPECT_NE(item.find(",score:"), std::string::npos) << item;
        // The word comes first and ends at the first comma: ZCode is
        // U+1865..U+18AA, so a comma cannot occur inside it.
        EXPECT_GT(item.find(','), 0U) << item;
    }
}

// A session mode must answer exactly what the stateless form answers, or a
// host would get different words depending on how it asked.
TEST_F(ProtocolDictionaryTest, SessionModesAgreeWithLookup) {
    EXPECT_EQ(Type("bagsi", "word"), Send("LOOKUP fuzzy bagsi"));
    EXPECT_EQ(Type("bagsi", "score"), Send("LOOKUP score bagsi"));
    EXPECT_EQ(Type("bagsi", "make"), Send("LOOKUP make bagsi"));
    EXPECT_EQ(Type("ene", "score"), Send("LOOKUP score ene"));
}

// The cap is what keeps a scored answer inside a host's response limit: the
// uncapped answer for some inputs is well over 100 KB.
TEST_F(ProtocolDictionaryTest, MaxCapsAScoredAnswerAndKeepsTheBestOnes) {
    const std::string capped = Type("ene", "score", "9");
    const std::vector<std::string> fields = Fields(capped);
    ASSERT_EQ(fields[0], "OK");
    EXPECT_EQ(fields.size(), 10U);  // OK plus nine items
    EXPECT_LT(capped.size(), 2048U);

    const std::vector<std::string> all = Fields(Type("ene", "score"));
    ASSERT_GT(all.size(), 10U);
    // Capping is a truncation of the ranked list, so the nine kept are the
    // nine best, in the same order.
    for (std::size_t i = 1; i < fields.size(); ++i) {
        EXPECT_EQ(fields[i], all[i]);
    }
}

TEST_F(ProtocolDictionaryTest, UntypeAndCandUseTheModeToo) {
    Type("bagsi", "score", "9");
    // Uncapped, because LOOKUP takes no cap of its own.
    EXPECT_EQ(Send("CAND 0 score"), Send("LOOKUP score bagsi"));
    // Backspace: the answer must be the one for the shorter input.
    EXPECT_EQ(Send("UNTYPE 0 score"), Send("LOOKUP score bags"));
}

// Shapes and words are different answers to the same keys; a host picks one.
TEST_F(ProtocolDictionaryTest, ShapeModesStillAnswerWithShapes) {
    EXPECT_EQ(Type("bagsi", "fuzzy"), Send("SHAPE fuzzy bagsi"));
    EXPECT_EQ(Type("bagsi", "severe"), Send("SHAPE severe bagsi"));
}

TEST_F(ProtocolDictionaryTest, EmptySessionLooksNothingUp) {
    bool miss = false;
    EXPECT_EQ(Send("CAND 0 score 9", &miss), "OK");
    // Nothing was asked of the dictionary, so this is not a miss: an
    // extension pack would be no help.
    EXPECT_FALSE(miss);
}

// What drives the optional extension pack's lazy load.
TEST_F(ProtocolDictionaryTest, AMissIsReportedOnlyWhenTheDictionaryHadNothing) {
    bool miss = true;
    EXPECT_EQ(Fields(Send("LOOKUP score bagsi", &miss))[0], "OK");
    EXPECT_FALSE(miss);

    // "bicig" is a real word the original's vocabulary does not hold.
    EXPECT_EQ(Send("LOOKUP score bicig", &miss), "OK");
    EXPECT_TRUE(miss);

    // The session modes report it the same way, which is what a host in
    // "score" mode relies on.
    miss = false;
    Type("bicig", "score", "9");
    EXPECT_EQ(Send("CAND 0 score 9", &miss), "OK");
    EXPECT_TRUE(miss);
}

TEST_F(ProtocolDictionaryTest, SevereLookupIsNeverAMiss) {
    bool miss = true;
    // No dictionary is consulted, so an empty answer says nothing about it.
    Send("LOOKUP severe bagsi", &miss);
    EXPECT_FALSE(miss);
}

// What an interactive host actually uses. "score" alone goes blank on a word
// the 38,743-word dictionary does not hold, which for an input method means
// a candidate window with nothing in it.
TEST_F(ProtocolDictionaryTest, FullIsScoreThatIsNeverEmpty) {
    // In the vocabulary: "full" is "score" plus any severe shape it lacks.
    const std::vector<std::string> score = Fields(Type("bagsi", "score"));
    const std::vector<std::string> full = Fields(Type("bagsi", "full"));
    ASSERT_GE(full.size(), score.size());
    for (std::size_t i = 0; i < score.size(); ++i) {
        EXPECT_EQ(full[i], score[i]) << i;
    }

    // Not in the vocabulary: "score" is empty where "full" still answers.
    EXPECT_EQ(Type("surghaghuli", "score"), "OK");
    const std::vector<std::string> rescued = Fields(Type("surghaghuli", "full"));
    ASSERT_GT(rescued.size(), 1U);
    // And what it answers with is exactly the severe shapes.
    const std::vector<std::string> shapes =
        Fields(Send("LOOKUP severe surghaghuli"));
    EXPECT_EQ(rescued.size(), shapes.size());
    for (std::size_t i = 1; i < rescued.size(); ++i) {
        EXPECT_EQ(rescued[i].substr(0, rescued[i].find(',')), shapes[i]) << i;
    }
}

// Every item is scored, so a host has one scale; a shape scores below any
// real word.
TEST_F(ProtocolDictionaryTest, FullScoresTheShapesZero) {
    for (const std::string& item : Fields(Type("surghaghuli", "full"))) {
        if (item == "OK") continue;
        EXPECT_NE(item.find(",score:0.0"), std::string::npos) << item;
    }
    bool saw_a_real_score = false;
    for (const std::string& item : Fields(Type("bagsi", "full"))) {
        if (item == "OK") continue;
        if (item.find(",score:1.0") != std::string::npos) saw_a_real_score = true;
    }
    EXPECT_TRUE(saw_a_real_score);
}

// The optional extension pack is loaded when the dictionary comes up empty.
// "full" answers anyway, so the miss has to be reported from the word list
// rather than from the answer, or the pack would never load in the one mode
// a host actually runs.
TEST_F(ProtocolDictionaryTest, FullStillReportsADictionaryMiss) {
    bool miss = false;
    Type("bicig", "full", "9");
    EXPECT_NE(Send("CAND 0 full 9", &miss), "OK");  // shapes came back
    EXPECT_TRUE(miss);

    miss = true;
    Type("bagsi", "full", "9");
    Send("CAND 0 full 9", &miss);
    EXPECT_FALSE(miss);
}

}  // namespace
}  // namespace zcode::service
