// SPDX-License-Identifier: GPL-2.0-or-later
//
// Java counterpart of bench/zcode_bench.cpp: the same workloads, on the same
// inputs (read from testdata/), against the Java original's LetterSplicer and
// Decomposer, printing the same candidate/code-point checksums.
//
//   java -cp <classpath> zcodecpp.golden.JavaBench <testdata-dir> [reps] [warmup]
//
// For every workload: the first run in this JVM (cold, before JIT warm-up),
// then `warmup` untimed runs, then min and median of `reps` timed runs.
package zcodecpp.golden;

import com.zorigt.ime.common.Strings;
import com.zorigt.ime.keyboard.Decomposer;
import com.zorigt.ime.letter.LetterShapeSequence;
import com.zorigt.ime.letter.splice.LetterSplicer;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class JavaBench {
    interface Workload {
        void run(long[] sum);  // sum[0] += candidates, sum[1] += code points
    }

    static LetterSplicer splicer;
    static Decomposer decomposer;

    static void add(long[] sum, List<LetterShapeSequence> seqs) {
        for (LetterShapeSequence s : seqs) {
            sum[0]++;
            sum[1] += s.toString().length();  // InputMethodService also materialises toString()
        }
    }

    // InputMethodService.severeMakeWord / the splicing half of fuzzyMakeWord
    static void shape(long[] sum, String input, boolean severe) {
        if (Strings.isBlank(input)) {
            return;
        }
        for (List<String> keys : decomposer.decompose(input)) {
            add(sum, severe ? splicer.severe(keys) : splicer.fuzzy(keys));
        }
    }

    static List<String[]> rows(File f) throws IOException {
        List<String[]> out = new ArrayList<>();
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (!line.isEmpty() && line.charAt(0) != '#') {
                    out.add(line.split("\t", -1));
                }
            }
        }
        return out;
    }

    // Undoes GoldenDump.escape()
    static String unescape(String s) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c != '\\') {
                sb.append(c);
                continue;
            }
            char e = s.charAt(++i);
            switch (e) {
                case 't': sb.append('\t'); break;
                case 'n': sb.append('\n'); break;
                case 'r': sb.append('\r'); break;
                case 'u': sb.append((char) Integer.parseInt(s.substring(i + 1, i + 5), 16)); i += 4; break;
                default: sb.append(e);
            }
        }
        return sb.toString();
    }

    static void run(String name, int reps, int warmup, Workload w) {
        long[] sum = new long[2];
        long t0 = System.nanoTime();
        w.run(sum);
        double cold = (System.nanoTime() - t0) / 1e6;
        for (int i = 0; i < warmup; i++) {
            w.run(new long[2]);
        }
        double[] ms = new double[reps];
        for (int i = 0; i < reps; i++) {
            long[] s = new long[2];
            long a = System.nanoTime();
            w.run(s);
            ms[i] = (System.nanoTime() - a) / 1e6;
            if (s[0] != sum[0] || s[1] != sum[1]) {
                throw new IllegalStateException("non-deterministic workload " + name);
            }
        }
        Arrays.sort(ms);
        System.out.printf("%-22s cold %9.2f ms  min %9.2f ms  median %9.2f ms  candidates %d  code_points %d%n",
                name, cold, ms[0], ms[reps / 2], sum[0], sum[1]);
    }

    public static void main(String[] args) throws Exception {
        long start = System.nanoTime();
        File dir = new File(args[0]);
        int reps = args.length > 1 ? Integer.parseInt(args[1]) : 9;
        int warmup = args.length > 2 ? Integer.parseInt(args[2]) : 5;

        // startup: class initialisation of Letters/rules/KeyboardLayout and the first result
        splicer = new LetterSplicer();
        decomposer = new Decomposer();
        long[] first = new long[2];
        shape(first, "mongol", true);
        System.out.printf("startup to first result  %.2f ms (JVM start not included)%n", (System.nanoTime() - start) / 1e6);

        final List<List<String>> keyLists = new ArrayList<>();
        for (String[] r : rows(new File(dir, "golden_splice.tsv"))) {
            if (r[0].equals("severe")) {
                keyLists.add(r[1].isEmpty() ? new ArrayList<String>() : Arrays.asList(r[1].split(" ")));
            }
        }
        final List<String> words = new ArrayList<>();
        for (String[] r : rows(new File(dir, "golden_e2e.tsv"))) {
            if (r[0].equals("severe")) {
                words.add(unescape(r[1]));
            }
        }
        final List<String> heavy = Arrays.asList("gggggg", "hghghg", "iiiiiiiiii", "hhhhhh", "hagalgagdahu", "gogugaga");
        final List<List<String>> typed = new ArrayList<>();
        for (String w : words) {
            List<List<String>> d = decomposer.decompose(w);
            if (d.size() == 1 && !d.get(0).isEmpty()) {
                typed.add(d.get(0));
            }
        }
        System.out.printf("%d key lists, %d words%n", keyLists.size(), words.size());

        run("splice_all_severe", reps, warmup, sum -> {
            for (List<String> k : keyLists) add(sum, splicer.severe(k));
        });
        run("splice_all_fuzzy", reps, warmup, sum -> {
            for (List<String> k : keyLists) add(sum, splicer.fuzzy(k));
        });
        run("typing_severe", reps, warmup, sum -> {
            for (String w : words) for (int n = 1; n <= w.length(); n++) shape(sum, w.substring(0, n), true);
        });
        run("typing_fuzzy", reps, warmup, sum -> {
            for (String w : words) for (int n = 1; n <= w.length(); n++) shape(sum, w.substring(0, n), false);
        });
        run("keys_stateless_severe", reps, warmup, sum -> {
            for (List<String> k : typed) for (int n = 1; n <= k.size(); n++) add(sum, splicer.severe(k.subList(0, n)));
        });
        run("keys_stateless_fuzzy", reps, warmup, sum -> {
            for (List<String> k : typed) for (int n = 1; n <= k.size(); n++) add(sum, splicer.fuzzy(k.subList(0, n)));
        });
        run("heavy_fuzzy", reps, warmup, sum -> {
            for (String w : heavy) shape(sum, w, false);
        });
    }
}
