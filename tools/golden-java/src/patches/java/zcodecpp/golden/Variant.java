// SPDX-License-Identifier: GPL-2.0-or-later
package zcodecpp.golden;

/** Which build of the Java original GoldenDump runs in: this one carries the zcode-cpp bug fixes. */
final class Variant {
    static final String NAME = "patched";
    /** The fixes in this source tree, one per replaced class (see each file's header). */
    static final String PATCHES = "none";

    private Variant() {
    }
}
