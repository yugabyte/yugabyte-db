// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.utils;

import java.util.Comparator;

/*
 * Implements a "natural comparator" for strings. For more information, see:
 * https://en.wikipedia.org/wiki/Natural_sort_order
 * This works by splitting the strings into blocks of only digits or only non-numeric characters
 * and then comparing them
 */
public class NaturalOrderComparator implements Comparator<String> {

  private static String getNaturalCompareBlock(String s, int startPos) {
    int length = s.length();
    boolean isDigitBlock = Character.isDigit(s.charAt(startPos));
    String block = "";
    while (startPos < length && Character.isDigit(s.charAt(startPos)) == isDigitBlock) {
      block += s.charAt(startPos);
      startPos++;
    }
    return block;
  }

  @Override
  public int compare(String a, String b) {
    int aLength = a.length(), bLength = b.length();
    int aPtr = 0, bPtr = 0;
    while (aPtr < aLength && bPtr < bLength) {
      String aBlock = getNaturalCompareBlock(a, aPtr);
      String bBlock = getNaturalCompareBlock(b, bPtr);
      aPtr += aBlock.length();
      bPtr += bBlock.length();
      int result = 0;
      if (Character.isDigit(aBlock.charAt(0)) && Character.isDigit(bBlock.charAt(0)))
        result = Integer.valueOf(aBlock).compareTo(Integer.valueOf(bBlock));
      else result = aBlock.compareToIgnoreCase(bBlock);
      if (result != 0) return result;
    }
    return aLength - bLength;
  }
}
