// SPDX-License-Identifier: GPL-2.0-or-later
//
// Golden-data generator for zcode-cpp.
//
// Calls the ORIGINAL east-mod/ime classes (LetterSplicer, Decomposer,
// KeyboardLayout, BTailKeyFilter, the rule holders) directly, with no Spring
// context and no database, and writes deterministic TSV files. Nothing in the
// output is hand-written: every expected value comes from running the Java
// original.
//
// Output format conventions (all files UTF-8, LF, '#' lines are comments):
//   * a ZCode string is written as its UTF-16 code units in upper-case hex,
//     joined by '.', e.g. "1865.186A";
//   * a list of candidates is joined by ' ' in the order Java produced them;
//   * a list longer than MAX_LISTED is replaced by "H:" + FNV-1a-64 (16 hex
//     digits) of the ASCII text the full list would have had;
//   * free-text inputs are escaped: '\\' -> "\\\\", TAB -> "\\t", LF -> "\\n",
//     CR -> "\\r", and every UTF-16 unit outside U+0020..U+007E -> "\\uXXXX".
package zcodecpp.golden;

import com.zorigt.ime.filter.BTailKeyFilter;
import com.zorigt.ime.keyboard.Decomposer;
import com.zorigt.ime.keyboard.KeyboardLayout;
import com.zorigt.ime.letter.Letter;
import com.zorigt.ime.letter.LetterLocation;
import com.zorigt.ime.letter.LetterShape;
import com.zorigt.ime.letter.LetterShapeSequence;
import com.zorigt.ime.letter.Letters;
import com.zorigt.ime.letter.Nature;
import com.zorigt.ime.letter.splice.BehindRuleHolder;
import com.zorigt.ime.letter.splice.FrontRuleHolder;
import com.zorigt.ime.letter.splice.LetterSplicer;
import com.zorigt.ime.letter.splice.SplicingAtBehindRule;
import com.zorigt.ime.letter.splice.SplicingAtFrontRule;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.IdentityHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;

public final class GoldenDump {
    static final int MAX_LISTED = 2000;
    static final long RANDOM_SEED = 20261020L;

    static final byte[] LOCATIONS = {LetterLocation.HEAD, LetterLocation.MIDDLE, LetterLocation.TAIL};

    // identity -> stable name, in Java reflection order
    final Map<LetterShape, String> shapeIds = new IdentityHashMap<>();
    final List<LetterShape> shapes = new ArrayList<>();
    final Map<Object, String> ruleNames = new IdentityHashMap<>();
    final List<SplicingAtFrontRule> frontRules = new ArrayList<>();
    final List<SplicingAtBehindRule> behindRules = new ArrayList<>();
    final Map<Letter, String> letterNames = new IdentityHashMap<>();
    List<String> keys;
    Map<String, List<Letter>> keyMap;

    final File outDir;

    GoldenDump(File outDir) {
        this.outDir = outDir;
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("usage: GoldenDump <output-dir>");
            System.exit(2);
        }
        File out = new File(args[0]);
        if (!out.isDirectory() && !out.mkdirs()) {
            throw new IOException("cannot create " + out);
        }
        GoldenDump g = new GoldenDump(out);
        g.collect();
        g.dumpShapes();
        g.dumpKeyboard();
        g.dumpFrontRules();
        g.dumpBehindRules();
        List<List<String>> spliceInputs = g.spliceInputs();
        g.dumpSplice(spliceInputs);
        List<String> textInputs = g.textInputs();
        g.dumpDecompose(textInputs);
        g.dumpEndToEnd(textInputs);
        g.dumpBTail();
        System.out.println("golden data written to " + out.getAbsolutePath());
    }

    // ---------------------------------------------------------------- collect

    @SuppressWarnings("unchecked")
    void collect() throws Exception {
        for (Field lf : Letters.class.getDeclaredFields()) {
            if (!Modifier.isStatic(lf.getModifiers()) || !Letter.class.isAssignableFrom(lf.getType())) {
                continue;
            }
            Letter letter = (Letter) lf.get(null);
            letterNames.put(letter, lf.getName());
            for (Field sf : letter.getClass().getDeclaredFields()) {
                if (!sf.getType().equals(LetterShape.class)) {
                    continue;
                }
                LetterShape ls = (LetterShape) sf.get(letter);
                shapeIds.put(ls, lf.getName() + "." + sf.getName());
                shapes.add(ls);
            }
        }
        for (Field f : FrontRuleHolder.class.getDeclaredFields()) {
            if (Modifier.isStatic(f.getModifiers()) && SplicingAtFrontRule.class.isAssignableFrom(f.getType())) {
                SplicingAtFrontRule r = (SplicingAtFrontRule) f.get(null);
                ruleNames.put(r, f.getName());
                frontRules.add(r);
            }
        }
        for (Field f : BehindRuleHolder.class.getDeclaredFields()) {
            if (Modifier.isStatic(f.getModifiers()) && SplicingAtBehindRule.class.isAssignableFrom(f.getType())) {
                SplicingAtBehindRule r = (SplicingAtBehindRule) f.get(null);
                ruleNames.put(r, f.getName());
                behindRules.add(r);
            }
        }
        Field kf = KeyboardLayout.class.getDeclaredField("KEYS");
        kf.setAccessible(true);
        keys = new ArrayList<>((List<String>) kf.get(null));
        Field mf = KeyboardLayout.class.getDeclaredField("keyMap");
        mf.setAccessible(true);
        keyMap = (Map<String, List<Letter>>) mf.get(null);
    }

    // ------------------------------------------------------------- formatting

    static String hex(String zcode) {
        StringBuilder sb = new StringBuilder(zcode.length() * 5);
        for (int i = 0; i < zcode.length(); i++) {
            if (i > 0) {
                sb.append('.');
            }
            sb.append(String.format("%04X", (int) zcode.charAt(i)));
        }
        return sb.toString();
    }

    static String escape(String s) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '\\': sb.append("\\\\"); break;
                case '\t': sb.append("\\t"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                default:
                    if (c >= 0x20 && c <= 0x7E) {
                        sb.append(c);
                    } else {
                        sb.append(String.format("\\u%04X", (int) c));
                    }
            }
        }
        return sb.toString();
    }

    static String fnv1a64(String ascii) {
        long h = 0xcbf29ce484222325L;
        for (int i = 0; i < ascii.length(); i++) {
            h ^= (ascii.charAt(i) & 0xFF);
            h *= 0x100000001b3L;
        }
        return String.format("%016X", h);
    }

    static String candidates(List<String> zcodes) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < zcodes.size(); i++) {
            if (i > 0) {
                sb.append(' ');
            }
            sb.append(hex(zcodes.get(i)));
        }
        String full = sb.toString();
        return zcodes.size() > MAX_LISTED ? "H:" + fnv1a64(full) : full;
    }

    static List<String> strings(List<LetterShapeSequence> seqs) {
        List<String> out = new ArrayList<>(seqs.size());
        for (LetterShapeSequence s : seqs) {
            out.add(s.toString());
        }
        return out;
    }

    String rule(Object r) {
        return r == null ? "-" : ruleNames.get(r);
    }

    static String location(byte loc) {
        switch (loc) {
            case LetterLocation.HEAD: return "HEAD";
            case LetterLocation.MIDDLE: return "MIDDLE";
            case LetterLocation.TAIL: return "TAIL";
            default: return "LOC" + loc;
        }
    }

    Writer open(String name, String... header) throws IOException {
        Writer w = new BufferedWriter(new OutputStreamWriter(
                new FileOutputStream(new File(outDir, name)), StandardCharsets.UTF_8));
        w.write("# SPDX-License-Identifier: GPL-2.0-or-later\n");
        w.write("# generated by tools/golden-java (GoldenDump) from the east-mod/ime Java original; do not edit\n");
        w.write("# java " + System.getProperty("java.version") + " (" + System.getProperty("java.vm.name") + ")\n");
        for (String h : header) {
            w.write("# " + h + "\n");
        }
        return w;
    }

    // ----------------------------------------------------------------- tables

    void dumpShapes() throws IOException {
        try (Writer w = open("java_shapes.tsv",
                "every LetterShape field of every Letters field, in getDeclaredFields() order",
                "equals_first: first shape (in this order) that LetterShape.equals() this one",
                "id\tlatin_key\tlocation\tzcode\tfuzzy_front\tfuzzy_behind\tsevere_front\tsevere_behind\tequals_first")) {
            for (LetterShape ls : shapes) {
                String eq = null;
                for (LetterShape o : shapes) {
                    if (o.equals(ls)) {
                        eq = shapeIds.get(o);
                        break;
                    }
                }
                w.write(shapeIds.get(ls) + "\t" + ls.getLatinKey() + "\t" + location(ls.getLocation()) + "\t"
                        + hex(ls.getShape()) + "\t"
                        + rule(ls.getFuzzyFrontAtRule()) + "\t" + rule(ls.getFuzzyBehindAtRule()) + "\t"
                        + rule(ls.getSevereFrontAtRule()) + "\t" + rule(ls.getSevereBehindAtRule()) + "\t"
                        + eq + "\n");
            }
        }
    }

    void dumpKeyboard() throws IOException {
        try (Writer w = open("java_keyboard.tsv",
                "KeyboardLayout.get(key, location) for every key in KeyboardLayout.KEYS order",
                "key\tletters\tlocation\tshapes")) {
            for (String key : keys) {
                StringBuilder letters = new StringBuilder();
                for (Letter l : keyMap.get(key)) {
                    if (letters.length() > 0) {
                        letters.append(' ');
                    }
                    letters.append(letterNames.get(l));
                }
                for (byte loc : LOCATIONS) {
                    StringBuilder sb = new StringBuilder();
                    for (LetterShape ls : KeyboardLayout.get(key, loc)) {
                        if (sb.length() > 0) {
                            sb.append(' ');
                        }
                        sb.append(shapeIds.get(ls));
                    }
                    w.write(key + "\t" + letters + "\t" + location(loc) + "\t" + sb + "\n");
                }
            }
        }
    }

    /**
     * Truth table of every front rule over every reachable (last shape, nature)
     * state, plus the empty sequence. States are built by appending real
     * shapes, so the nature column is whatever LetterShapeSequence computed.
     */
    void dumpFrontRules() throws IOException {
        LetterShape[] prefixes = {null, Letters.A.aHead, Letters.E.eHead};
        try (Writer w = open("java_rules_front.tsv",
                "SplicingAtFrontRule.match(lss) for lss = empty, and lss = [prefix] + last",
                "with prefix in {none, A.aHead, E.eHead}; duplicate (last, nature) states are omitted",
                "rule\tlast\tnature\tresult")) {
            for (SplicingAtFrontRule r : frontRules) {
                LetterShapeSequence empty = new LetterShapeSequence();
                w.write(rule(r) + "\tEMPTY\t" + empty.getNature() + "\t" + (r.match(empty) ? 1 : 0) + "\n");
                for (LetterShape last : shapes) {
                    Set<Nature> seen = new LinkedHashSet<>();
                    for (LetterShape prefix : prefixes) {
                        LetterShapeSequence lss = new LetterShapeSequence();
                        if (prefix != null) {
                            lss.append(prefix);
                        }
                        lss.append(last);
                        if (!seen.add(lss.getNature())) {
                            continue;
                        }
                        w.write(rule(r) + "\t" + shapeIds.get(last) + "\t" + lss.getNature() + "\t"
                                + (r.match(lss) ? 1 : 0) + "\n");
                    }
                }
            }
        }
    }

    void dumpBehindRules() throws IOException {
        try (Writer w = open("java_rules_behind.tsv",
                "SplicingAtBehindRule.match(next) for every shape (Verifier never passes null)",
                "rule\tnext\tresult")) {
            for (SplicingAtBehindRule r : behindRules) {
                for (LetterShape next : shapes) {
                    w.write(rule(r) + "\t" + shapeIds.get(next) + "\t" + (r.match(next) ? 1 : 0) + "\n");
                }
            }
        }
    }

    // ---------------------------------------------------------------- splicer

    List<List<String>> spliceInputs() {
        List<List<String>> in = new ArrayList<>();
        // exhaustive: every key sequence of length 1..3
        for (String a : keys) {
            in.add(Arrays.asList(a));
        }
        for (String a : keys) {
            for (String b : keys) {
                in.add(Arrays.asList(a, b));
            }
        }
        for (String a : keys) {
            for (String b : keys) {
                for (String c : keys) {
                    in.add(Arrays.asList(a, b, c));
                }
            }
        }
        // deterministic pseudo-random sequences of length 4..8
        Random rnd = new Random(RANDOM_SEED);
        for (int n = 0; n < 3000; n++) {
            int len = 4 + rnd.nextInt(5);
            List<String> s = new ArrayList<>(len);
            for (int i = 0; i < len; i++) {
                s.add(keys.get(rnd.nextInt(keys.size())));
            }
            in.add(s);
        }
        // keys the splicer does not know (exercises the 'continue' path) and
        // keys with no shape at some location
        String[][] edge = {
                {}, {"ng"}, {"ng", "a"}, {"a", "ng"}, {"a", "ng", "a"}, {"A"}, {"A", "a"}, {"a", "A", "e"},
                {"v"}, {"a", "v", "a"}, {"R", "a"}, {"a", "R"}, {"a", "R", "a"}, {"a", "L", "a"},
                {"a", "Z"}, {"a", "C", "a"}, {"a", "H"}, {"a", "H", "a"}, {"H", "H", "H"},
                {"R", "L", "Z", "C", "H"}, {"?", "a"}, {"a", "?"},
        };
        for (String[] e : edge) {
            in.add(Arrays.asList(e));
        }
        return in;
    }

    void dumpSplice(List<List<String>> inputs) throws IOException {
        LetterSplicer splicer = new LetterSplicer();
        try (Writer w = open("golden_splice.tsv",
                "LetterSplicer.severe / fuzzy called directly on a key list (keys joined by ' ')",
                "inputs: all length 1..3 sequences over KeyboardLayout.KEYS, 3000 random length 4..8"
                        + " sequences (java.util.Random seed " + RANDOM_SEED + "), and edge cases",
                "mode\tkeys\tcount\tcandidates")) {
            for (List<String> keyList : inputs) {
                String joined = String.join(" ", keyList);
                for (String mode : new String[]{"severe", "fuzzy"}) {
                    List<String> out = strings("severe".equals(mode)
                            ? splicer.severe(keyList) : splicer.fuzzy(keyList));
                    w.write(mode + "\t" + joined + "\t" + out.size() + "\t" + candidates(out) + "\n");
                }
            }
        }
    }

    // ------------------------------------------------------- text / decompose

    List<String> textInputs() {
        List<String> in = new ArrayList<>();
        String[] words = {
                // ordinary transliterations as typed on this layout (no 'v' key)
                "mongol", "monggol", "bagsi", "baga", "ehe", "aha", "nom", "sain", "bain", "baina",
                "hele", "bichig", "bicig", "olan", "ulus", "gal", "gol", "gegen", "gerel", "tenggeri",
                "tngri", "hoyar", "nige", "gurban", "dorben", "tabun", "jirgugan", "doloon", "naiman",
                "yisun", "arban", "morin", "temur", "sara", "nara", "usun", "modon", "cagan", "hara",
                "ulagan", "sira", "hoh", "nogogan", "ger", "mal", "honi", "uher", "temege", "yamaga",
                "nohai", "miqi", "gahai", "takiya", "bars", "arslan", "cino", "ab", "ob", "ub", "eb",
                "bab", "hob", "gub", "nab", "ag", "eg", "og", "ug", "agsa", "egse", "aga", "ege",
                "sda", "sga", "dga", "sge", "dge", "ama", "eme", "ala", "ele", "ara", "ere", "aya", "eye",
                "ana", "ene", "ii", "aii", "eii", "bii", "hii", "giui", "bu", "pu", "hu", "gu", "fu", "ku",
                "bo", "po", "ho", "go", "fo", "ko", "boo", "buu", "hogo", "gugu", "iu", "ui",
                // galig letters
                "Hara", "Ralf", "Lama", "Zungwa", "Coqi", "aH", "aR", "aL", "aZ", "aC", "HaH", "Ha",
                // edge cases
                "", " ", "  ", "a b", "a  b", " a", "a ", "\t", "a\tb", "A", "ABC", "aBc", "1a2", "ng", "nga",
                "é", "aéb", "abᠠc", "😀a", "a😀", "-", "a-b", "v", "va",
                "aaaaaaaaaaaaaaaaaaaa", "eeeeeeeeeeeeeeeeeeee", "iiiiiiiiii", "uuuuuuuuuu", "oooooooooo",
                "nnnnnnnn", "hhhhhh", "gggggg", "hghghg", "bbbbbbbb", "mmmmmmmm", "tdtdtdtd",
        };
        in.addAll(Arrays.asList(words));
        Random rnd = new Random(RANDOM_SEED + 1);
        String alphabet = "aeiounbphgmlsxtdqjyrwfkczHRLZCAvV1 -";
        for (int n = 0; n < 500; n++) {
            int len = 1 + rnd.nextInt(8);
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < len; i++) {
                sb.append(alphabet.charAt(rnd.nextInt(alphabet.length())));
            }
            in.add(sb.toString());
        }
        return in;
    }

    void dumpDecompose(List<String> inputs) throws IOException {
        Decomposer decomposer = new Decomposer();
        try (Writer w = open("golden_decompose.tsv",
                "Decomposer.decompose(input); decompositions joined by '|', keys within one joined by ' '",
                "input(escaped)\tcount\tdecompositions")) {
            for (String s : inputs) {
                List<List<String>> d = decomposer.decompose(s);
                StringBuilder sb = new StringBuilder();
                for (int i = 0; i < d.size(); i++) {
                    if (i > 0) {
                        sb.append('|');
                    }
                    sb.append(String.join(" ", d.get(i)));
                }
                w.write(escape(s) + "\t" + d.size() + "\t" + sb + "\n");
            }
        }
    }

    /**
     * Mirrors InputMethodService.severeMakeWord (and the splicing half of
     * fuzzyMakeWord): decompose, then splice every decomposition and
     * concatenate the results in order. Blank input yields nothing, exactly as
     * the service's Strings.isBlank guard does.
     */
    void dumpEndToEnd(List<String> inputs) throws IOException {
        Decomposer decomposer = new Decomposer();
        LetterSplicer splicer = new LetterSplicer();
        try (Writer w = open("golden_e2e.tsv",
                "Strings.isBlank guard + Decomposer.decompose + LetterSplicer over each decomposition, concatenated",
                "mode\tinput(escaped)\tcount\tcandidates")) {
            for (String s : inputs) {
                for (String mode : new String[]{"severe", "fuzzy"}) {
                    List<String> out = new ArrayList<>();
                    if (!com.zorigt.ime.common.Strings.isBlank(s)) {
                        for (List<String> keyList : decomposer.decompose(s)) {
                            out.addAll(strings("severe".equals(mode)
                                    ? splicer.severe(keyList) : splicer.fuzzy(keyList)));
                        }
                    }
                    w.write(mode + "\t" + escape(s) + "\t" + out.size() + "\t" + candidates(out) + "\n");
                }
            }
        }
    }

    // ------------------------------------------------------------------ filter

    void dumpBTail() throws IOException {
        BTailKeyFilter f = new BTailKeyFilter();
        char baga = com.zorigt.ime.letter.ZCode.b_banbvr_baga;
        char ih = com.zorigt.ime.letter.ZCode.b_banbvr_ih;
        char egs = com.zorigt.ime.letter.ZCode.b_banbvr_egsilge;
        char t = com.zorigt.ime.letter.ZCode.titem;
        char a = com.zorigt.ime.letter.ZCode.achvg;
        Set<String> in = new LinkedHashSet<>();
        String[] synthetic = {
                "", String.valueOf(baga), String.valueOf(ih), String.valueOf(egs),
                "" + t + baga, "" + t + ih, "" + t + egs, "" + t + a + baga, "" + baga + a + baga,
                "" + ih + a + ih, "" + t + baga + a, "" + baga + baga, "" + t + baga + baga, "" + ih + baga,
                "" + t + a + "\n" + baga, "x" + baga, " " + baga,
        };
        in.addAll(Arrays.asList(synthetic));
        // every severe candidate of every length-1..3 key sequence ending in a vowel or 'b'
        LetterSplicer splicer = new LetterSplicer();
        for (String k1 : keys) {
            for (String k2 : keys) {
                for (String k3 : new String[]{"a", "e", "o", "u", "b"}) {
                    for (LetterShapeSequence s : splicer.severe(Arrays.asList(k1, k2, k3))) {
                        in.add(s.toString());
                    }
                    for (LetterShapeSequence s : splicer.fuzzy(Arrays.asList(k1, k2, k3))) {
                        in.add(s.toString());
                    }
                }
            }
        }
        try (Writer w = open("golden_btail.tsv",
                "BTailKeyFilter.accept(zcode) (key \"" + f.getKey() + "\"); zcode written as UTF-16 hex, '-' for empty",
                "zcode\taccept")) {
            for (String s : in) {
                w.write((s.isEmpty() ? "-" : hex(s)) + "\t" + (f.accept(s) ? 1 : 0) + "\n");
            }
        }
    }
}
