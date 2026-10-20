// SPDX-License-Identifier: GPL-2.0-or-later
//
// Copy of com.zorigt.ime.letter.splice.LetterSplicer from east-mod/ime
// (commit cad6a24, by Zorigt) with one zcode-cpp bug fix, applied identically
// to fuzzy() and severe():
//
// The original starts a new sequence from the empty one whenever
// completeLssList is empty. That is right before the first key that has
// shapes, but after a key whose candidates were all rejected it restarts
// splicing in the middle of the word and returns fragments of its tail
// (e.g. severe [j k R u i t C] -> just "t"). Now only the first key that has
// shapes starts sequences (`started`); once every candidate has been
// rejected the result stays empty. Keys without shapes at their position are
// still skipped as before.
package com.zorigt.ime.letter.splice;

import com.google.common.collect.Lists;
import com.zorigt.ime.keyboard.KeyboardLayout;
import com.zorigt.ime.letter.LetterLocation;
import com.zorigt.ime.letter.LetterShape;
import com.zorigt.ime.letter.LetterShapeSequence;
import org.apache.commons.collections.CollectionUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.time.Instant;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedList;
import java.util.List;

public class LetterSplicer {
    private static final transient Logger logger = LoggerFactory.getLogger(LetterSplicer.class);

    public LetterSplicer() {
    }

    public List<LetterShapeSequence> fuzzy(List<String> latinSequence) {
        long time1 = Instant.now().toEpochMilli();
        try {
            if (CollectionUtils.isEmpty(latinSequence)) {
                return Collections.emptyList();
            }
            List<LetterShapeSequence> completeLssList = new ArrayList<>();
            int i = 0;
            boolean started = false; // zcode-cpp fix
            for (String key : latinSequence) {
                List<LetterShape> letterShapeList = new LinkedList<>();
                if (i == 0) {
                    letterShapeList.addAll(KeyboardLayout.get(key, LetterLocation.HEAD));
                } else if (i == latinSequence.size() - 1) {
                    letterShapeList.addAll(KeyboardLayout.get(key, LetterLocation.MIDDLE));
                    letterShapeList.addAll(KeyboardLayout.get(key, LetterLocation.TAIL));
                } else {
                    letterShapeList.addAll(KeyboardLayout.get(key, LetterLocation.MIDDLE));
                }
                i++;

                if (CollectionUtils.isEmpty(letterShapeList)) {
                    continue;
                }
                int size = (completeLssList.size() > 0 ? completeLssList.size() : 1) * letterShapeList.size();
                List<LetterShapeSequence> newWholeLssList = Lists.newArrayListWithExpectedSize(size);

                LetterShapeSequence lss;
                if (!started) { // zcode-cpp fix: was CollectionUtils.isEmpty(completeLssList)
                    for (LetterShape letterShape : letterShapeList) {
                        if (Verifier.canFuzzySplicing(LetterShapeSequence.emptyLss(), letterShape)) {
                            lss = new LetterShapeSequence();
                            lss.append(letterShape);
                            newWholeLssList.add(lss);
                        } else {
                            logger.info("ignored:{}", letterShape);
                        }
                    }
                } else {
                    for (LetterShape letterShape : letterShapeList) {
                        for (LetterShapeSequence clss : completeLssList) {
                            if (Verifier.canFuzzySplicing(clss, letterShape)) {
                                lss = new LetterShapeSequence();
                                lss.append(clss);
                                lss.append(letterShape);
                                newWholeLssList.add(lss);
                            } else {
                                logger.info("ignored:{},{}", clss.toString(), letterShape);
                            }
                        }
                    }
                }
                started = true; // zcode-cpp fix
                completeLssList = newWholeLssList;
            }
            return completeLssList;
        } finally {
            logger.info("latin to zcode run time:{}", Instant.now().toEpochMilli() - time1);
        }
    }

    public List<LetterShapeSequence> severe(List<String> latinSequence) {
        long time1 = Instant.now().toEpochMilli();
        try {
            if (CollectionUtils.isEmpty(latinSequence)) {
                return Collections.emptyList();
            }
            List<LetterShapeSequence> completeLssList = new ArrayList<>();
            int i = 0;
            boolean started = false; // zcode-cpp fix
            for (String key : latinSequence) {
                List<LetterShape> letterShapeList = new LinkedList<>();
                if (i == 0) {
                    letterShapeList.addAll(KeyboardLayout.get(key, LetterLocation.HEAD));
                } else if (i == latinSequence.size() - 1) {
                    letterShapeList.addAll(KeyboardLayout.get(key, LetterLocation.TAIL));
                } else {
                    letterShapeList.addAll(KeyboardLayout.get(key, LetterLocation.MIDDLE));
                }
                i++;

                if (CollectionUtils.isEmpty(letterShapeList)) {
                    continue;
                }
                int size = (completeLssList.size() > 0 ? completeLssList.size() : 1) * letterShapeList.size();
                List<LetterShapeSequence> newWholeLssList = Lists.newArrayListWithExpectedSize(size);

                LetterShapeSequence lss;
                if (!started) { // zcode-cpp fix: was CollectionUtils.isEmpty(completeLssList)
                    for (LetterShape letterShape : letterShapeList) {
                        if (Verifier.canSevereSplicing(LetterShapeSequence.emptyLss(), letterShape)) {
                            lss = new LetterShapeSequence();
                            lss.append(letterShape);
                            newWholeLssList.add(lss);
                        } else {
                            logger.info("ignored:{}", letterShape);
                        }
                    }
                } else {
                    for (LetterShape letterShape : letterShapeList) {
                        for (LetterShapeSequence clss : completeLssList) {
                            if (Verifier.canSevereSplicing(clss, letterShape)) {
                                lss = new LetterShapeSequence();
                                lss.append(clss);
                                lss.append(letterShape);
                                newWholeLssList.add(lss);
                            } else {
                                logger.info("ignored:{},{}", clss.toString(), letterShape);
                            }
                        }
                    }
                }
                started = true; // zcode-cpp fix
                completeLssList = newWholeLssList;
            }
            return completeLssList;
        } finally {
            logger.info("latin to zcode run time:{}", Instant.now().toEpochMilli() - time1);
        }
    }
}
