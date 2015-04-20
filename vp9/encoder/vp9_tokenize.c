/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "vpx_mem/vpx_mem.h"

#include "vp9/common/vp9_entropy.h"
#include "vp9/common/vp9_pred_common.h"
#include "vp9/common/vp9_seg_common.h"

#include "vp9/encoder/vp9_cost.h"
#include "vp9/encoder/vp9_encoder.h"
#include "vp9/encoder/vp9_tokenize.h"

static const TOKENVALUE dct_cat_lt_10_value_tokens[] = {
  {9, 63}, {9, 61}, {9, 59}, {9, 57}, {9, 55}, {9, 53}, {9, 51}, {9, 49},
  {9, 47}, {9, 45}, {9, 43}, {9, 41}, {9, 39}, {9, 37}, {9, 35}, {9, 33},
  {9, 31}, {9, 29}, {9, 27}, {9, 25}, {9, 23}, {9, 21}, {9, 19}, {9, 17},
  {9, 15}, {9, 13}, {9, 11}, {9, 9}, {9, 7}, {9, 5}, {9, 3}, {9, 1},
  {8, 31}, {8, 29}, {8, 27}, {8, 25}, {8, 23}, {8, 21},
  {8, 19}, {8, 17}, {8, 15}, {8, 13}, {8, 11}, {8, 9},
  {8, 7}, {8, 5}, {8, 3}, {8, 1},
  {7, 15}, {7, 13}, {7, 11}, {7, 9}, {7, 7}, {7, 5}, {7, 3}, {7, 1},
  {6, 7}, {6, 5}, {6, 3}, {6, 1}, {5, 3}, {5, 1},
  {4, 1}, {3, 1}, {2, 1}, {1, 1}, {0, 0},
  {1, 0},  {2, 0}, {3, 0}, {4, 0},
  {5, 0}, {5, 2}, {6, 0}, {6, 2}, {6, 4}, {6, 6},
  {7, 0}, {7, 2}, {7, 4}, {7, 6}, {7, 8}, {7, 10}, {7, 12}, {7, 14},
  {8, 0}, {8, 2}, {8, 4}, {8, 6}, {8, 8}, {8, 10}, {8, 12},
  {8, 14}, {8, 16}, {8, 18}, {8, 20}, {8, 22}, {8, 24},
  {8, 26}, {8, 28}, {8, 30}, {9, 0}, {9, 2},
  {9, 4}, {9, 6}, {9, 8}, {9, 10}, {9, 12}, {9, 14}, {9, 16},
  {9, 18}, {9, 20}, {9, 22}, {9, 24}, {9, 26}, {9, 28},
  {9, 30}, {9, 32}, {9, 34}, {9, 36}, {9, 38}, {9, 40},
  {9, 42}, {9, 44}, {9, 46}, {9, 48}, {9, 50}, {9, 52},
  {9, 54}, {9, 56}, {9, 58}, {9, 60}, {9, 62}
};
const TOKENVALUE *vp9_dct_cat_lt_10_value_tokens = dct_cat_lt_10_value_tokens +
    (sizeof(dct_cat_lt_10_value_tokens) / sizeof(*dct_cat_lt_10_value_tokens))
    / 2;

// Array indices are identical to previously-existing CONTEXT_NODE indices
const vp9_tree_index vp9_coef_tree[TREE_SIZE(ENTROPY_TOKENS)] = {
  -EOB_TOKEN, 2,                       // 0  = EOB
  -ZERO_TOKEN, 4,                      // 1  = ZERO
  -ONE_TOKEN, 6,                       // 2  = ONE
  8, 12,                               // 3  = LOW_VAL
  -TWO_TOKEN, 10,                      // 4  = TWO
  -THREE_TOKEN, -FOUR_TOKEN,           // 5  = THREE
  14, 16,                              // 6  = HIGH_LOW
  -CATEGORY1_TOKEN, -CATEGORY2_TOKEN,  // 7  = CAT_ONE
  18, 20,                              // 8  = CAT_THREEFOUR
  -CATEGORY3_TOKEN, -CATEGORY4_TOKEN,  // 9  = CAT_THREE
  -CATEGORY5_TOKEN, -CATEGORY6_TOKEN   // 10 = CAT_FIVE
};

static const vp9_tree_index cat1[2] = {0, 0};
static const vp9_tree_index cat2[4] = {2, 2, 0, 0};
static const vp9_tree_index cat3[6] = {2, 2, 4, 4, 0, 0};
static const vp9_tree_index cat4[8] = {2, 2, 4, 4, 6, 6, 0, 0};
static const vp9_tree_index cat5[10] = {2, 2, 4, 4, 6, 6, 8, 8, 0, 0};
static const vp9_tree_index cat6[28] = {2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, 12,
    14, 14, 16, 16, 18, 18, 20, 20, 22, 22, 24, 24, 26, 26, 0, 0};

static const int16_t zero_cost[] = {0};
static const int16_t one_cost[] = {255, 257};
static const int16_t two_cost[] = {255, 257};
static const int16_t three_cost[] = {255, 257};
static const int16_t four_cost[] = {255, 257};
static const int16_t cat1_cost[] = {429, 431, 616, 618};
static const int16_t cat2_cost[] = {624, 626, 727, 729, 848, 850, 951, 953};
static const int16_t cat3_cost[] = {
  820, 822, 893, 895, 940, 942, 1013, 1015, 1096, 1098, 1169, 1171, 1216, 1218,
  1289, 1291
};
static const int16_t cat4_cost[] = {
  1032, 1034, 1075, 1077, 1105, 1107, 1148, 1150, 1194, 1196, 1237, 1239,
  1267, 1269, 1310, 1312, 1328, 1330, 1371, 1373, 1401, 1403, 1444, 1446,
  1490, 1492, 1533, 1535, 1563, 1565, 1606, 1608
};
static const int16_t cat5_cost[] = {
  1269, 1271, 1283, 1285, 1306, 1308, 1320,
  1322, 1347, 1349, 1361, 1363, 1384, 1386, 1398, 1400, 1443, 1445, 1457,
  1459, 1480, 1482, 1494, 1496, 1521, 1523, 1535, 1537, 1558, 1560, 1572,
  1574, 1592, 1594, 1606, 1608, 1629, 1631, 1643, 1645, 1670, 1672, 1684,
  1686, 1707, 1709, 1721, 1723, 1766, 1768, 1780, 1782, 1803, 1805, 1817,
  1819, 1844, 1846, 1858, 1860, 1881, 1883, 1895, 1897
};
const int16_t vp9_cat6_low_cost[256] = {
  1638, 1640, 1646, 1648, 1652, 1654, 1660, 1662,
  1670, 1672, 1678, 1680, 1684, 1686, 1692, 1694, 1711, 1713, 1719, 1721,
  1725, 1727, 1733, 1735, 1743, 1745, 1751, 1753, 1757, 1759, 1765, 1767,
  1787, 1789, 1795, 1797, 1801, 1803, 1809, 1811, 1819, 1821, 1827, 1829,
  1833, 1835, 1841, 1843, 1860, 1862, 1868, 1870, 1874, 1876, 1882, 1884,
  1892, 1894, 1900, 1902, 1906, 1908, 1914, 1916, 1940, 1942, 1948, 1950,
  1954, 1956, 1962, 1964, 1972, 1974, 1980, 1982, 1986, 1988, 1994, 1996,
  2013, 2015, 2021, 2023, 2027, 2029, 2035, 2037, 2045, 2047, 2053, 2055,
  2059, 2061, 2067, 2069, 2089, 2091, 2097, 2099, 2103, 2105, 2111, 2113,
  2121, 2123, 2129, 2131, 2135, 2137, 2143, 2145, 2162, 2164, 2170, 2172,
  2176, 2178, 2184, 2186, 2194, 2196, 2202, 2204, 2208, 2210, 2216, 2218,
  2082, 2084, 2090, 2092, 2096, 2098, 2104, 2106, 2114, 2116, 2122, 2124,
  2128, 2130, 2136, 2138, 2155, 2157, 2163, 2165, 2169, 2171, 2177, 2179,
  2187, 2189, 2195, 2197, 2201, 2203, 2209, 2211, 2231, 2233, 2239, 2241,
  2245, 2247, 2253, 2255, 2263, 2265, 2271, 2273, 2277, 2279, 2285, 2287,
  2304, 2306, 2312, 2314, 2318, 2320, 2326, 2328, 2336, 2338, 2344, 2346,
  2350, 2352, 2358, 2360, 2384, 2386, 2392, 2394, 2398, 2400, 2406, 2408,
  2416, 2418, 2424, 2426, 2430, 2432, 2438, 2440, 2457, 2459, 2465, 2467,
  2471, 2473, 2479, 2481, 2489, 2491, 2497, 2499, 2503, 2505, 2511, 2513,
  2533, 2535, 2541, 2543, 2547, 2549, 2555, 2557, 2565, 2567, 2573, 2575,
  2579, 2581, 2587, 2589, 2606, 2608, 2614, 2616, 2620, 2622, 2628, 2630,
  2638, 2640, 2646, 2648, 2652, 2654, 2660, 2662
};
const int16_t vp9_cat6_high_cost[128] = {
  72, 892, 1183, 2003, 1448, 2268, 2559, 3379,
  1709, 2529, 2820, 3640, 3085, 3905, 4196, 5016, 2118, 2938, 3229, 4049,
  3494, 4314, 4605, 5425, 3755, 4575, 4866, 5686, 5131, 5951, 6242, 7062,
  2118, 2938, 3229, 4049, 3494, 4314, 4605, 5425, 3755, 4575, 4866, 5686,
  5131, 5951, 6242, 7062, 4164, 4984, 5275, 6095, 5540, 6360, 6651, 7471,
  5801, 6621, 6912, 7732, 7177, 7997, 8288, 9108, 2118, 2938, 3229, 4049,
  3494, 4314, 4605, 5425, 3755, 4575, 4866, 5686, 5131, 5951, 6242, 7062,
  4164, 4984, 5275, 6095, 5540, 6360, 6651, 7471, 5801, 6621, 6912, 7732,
  7177, 7997, 8288, 9108, 4164, 4984, 5275, 6095, 5540, 6360, 6651, 7471,
  5801, 6621, 6912, 7732, 7177, 7997, 8288, 9108, 6210, 7030, 7321, 8141,
  7586, 8406, 8697, 9517, 7847, 8667, 8958, 9778, 9223, 10043, 10334, 11154
};

#if CONFIG_VP9_HIGHBITDEPTH
const int16_t vp9_cat6_high10_high_cost[512] = {
  74, 894, 1185, 2005, 1450, 2270, 2561,
  3381, 1711, 2531, 2822, 3642, 3087, 3907, 4198, 5018, 2120, 2940, 3231,
  4051, 3496, 4316, 4607, 5427, 3757, 4577, 4868, 5688, 5133, 5953, 6244,
  7064, 2120, 2940, 3231, 4051, 3496, 4316, 4607, 5427, 3757, 4577, 4868,
  5688, 5133, 5953, 6244, 7064, 4166, 4986, 5277, 6097, 5542, 6362, 6653,
  7473, 5803, 6623, 6914, 7734, 7179, 7999, 8290, 9110, 2120, 2940, 3231,
  4051, 3496, 4316, 4607, 5427, 3757, 4577, 4868, 5688, 5133, 5953, 6244,
  7064, 4166, 4986, 5277, 6097, 5542, 6362, 6653, 7473, 5803, 6623, 6914,
  7734, 7179, 7999, 8290, 9110, 4166, 4986, 5277, 6097, 5542, 6362, 6653,
  7473, 5803, 6623, 6914, 7734, 7179, 7999, 8290, 9110, 6212, 7032, 7323,
  8143, 7588, 8408, 8699, 9519, 7849, 8669, 8960, 9780, 9225, 10045, 10336,
  11156, 2120, 2940, 3231, 4051, 3496, 4316, 4607, 5427, 3757, 4577, 4868,
  5688, 5133, 5953, 6244, 7064, 4166, 4986, 5277, 6097, 5542, 6362, 6653,
  7473, 5803, 6623, 6914, 7734, 7179, 7999, 8290, 9110, 4166, 4986, 5277,
  6097, 5542, 6362, 6653, 7473, 5803, 6623, 6914, 7734, 7179, 7999, 8290,
  9110, 6212, 7032, 7323, 8143, 7588, 8408, 8699, 9519, 7849, 8669, 8960,
  9780, 9225, 10045, 10336, 11156, 4166, 4986, 5277, 6097, 5542, 6362, 6653,
  7473, 5803, 6623, 6914, 7734, 7179, 7999, 8290, 9110, 6212, 7032, 7323,
  8143, 7588, 8408, 8699, 9519, 7849, 8669, 8960, 9780, 9225, 10045, 10336,
  11156, 6212, 7032, 7323, 8143, 7588, 8408, 8699, 9519, 7849, 8669, 8960,
  9780, 9225, 10045, 10336, 11156, 8258, 9078, 9369, 10189, 9634, 10454,
  10745, 11565, 9895, 10715, 11006, 11826, 11271, 12091, 12382, 13202, 2120,
  2940, 3231, 4051, 3496, 4316, 4607, 5427, 3757, 4577, 4868, 5688, 5133,
  5953, 6244, 7064, 4166, 4986, 5277, 6097, 5542, 6362, 6653, 7473, 5803,
  6623, 6914, 7734, 7179, 7999, 8290, 9110, 4166, 4986, 5277, 6097, 5542,
  6362, 6653, 7473, 5803, 6623, 6914, 7734, 7179, 7999, 8290, 9110, 6212,
  7032, 7323, 8143, 7588, 8408, 8699, 9519, 7849, 8669, 8960, 9780, 9225,
  10045, 10336, 11156, 4166, 4986, 5277, 6097, 5542, 6362, 6653, 7473, 5803,
  6623, 6914, 7734, 7179, 7999, 8290, 9110, 6212, 7032, 7323, 8143, 7588,
  8408, 8699, 9519, 7849, 8669, 8960, 9780, 9225, 10045, 10336, 11156, 6212,
  7032, 7323, 8143, 7588, 8408, 8699, 9519, 7849, 8669, 8960, 9780, 9225,
  10045, 10336, 11156, 8258, 9078, 9369, 10189, 9634, 10454, 10745, 11565,
  9895, 10715, 11006, 11826, 11271, 12091, 12382, 13202, 4166, 4986, 5277,
  6097, 5542, 6362, 6653, 7473, 5803, 6623, 6914, 7734, 7179, 7999, 8290,
  9110, 6212, 7032, 7323, 8143, 7588, 8408, 8699, 9519, 7849, 8669, 8960,
  9780, 9225, 10045, 10336, 11156, 6212, 7032, 7323, 8143, 7588, 8408, 8699,
  9519, 7849, 8669, 8960, 9780, 9225, 10045, 10336, 11156, 8258, 9078, 9369,
  10189, 9634, 10454, 10745, 11565, 9895, 10715, 11006, 11826, 11271, 12091,
  12382, 13202, 6212, 7032, 7323, 8143, 7588, 8408, 8699, 9519, 7849, 8669,
  8960, 9780, 9225, 10045, 10336, 11156, 8258, 9078, 9369, 10189, 9634, 10454,
  10745, 11565, 9895, 10715, 11006, 11826, 11271, 12091, 12382, 13202, 8258,
  9078, 9369, 10189, 9634, 10454, 10745, 11565, 9895, 10715, 11006, 11826,
  11271, 12091, 12382, 13202, 10304, 11124, 11415, 12235, 11680, 12500, 12791,
  13611, 11941, 12761, 13052, 13872, 13317, 14137, 14428, 15248,
};
const int16_t vp9_cat6_high12_high_cost[2048] = {
  76, 896, 1187, 2007, 1452, 2272, 2563,
  3383, 1713, 2533, 2824, 3644, 3089, 3909, 4200, 5020, 2122, 2942, 3233,
  4053, 3498, 4318, 4609, 5429, 3759, 4579, 4870, 5690, 5135, 5955, 6246,
  7066, 2122, 2942, 3233, 4053, 3498, 4318, 4609, 5429, 3759, 4579, 4870,
  5690, 5135, 5955, 6246, 7066, 4168, 4988, 5279, 6099, 5544, 6364, 6655,
  7475, 5805, 6625, 6916, 7736, 7181, 8001, 8292, 9112, 2122, 2942, 3233,
  4053, 3498, 4318, 4609, 5429, 3759, 4579, 4870, 5690, 5135, 5955, 6246,
  7066, 4168, 4988, 5279, 6099, 5544, 6364, 6655, 7475, 5805, 6625, 6916,
  7736, 7181, 8001, 8292, 9112, 4168, 4988, 5279, 6099, 5544, 6364, 6655,
  7475, 5805, 6625, 6916, 7736, 7181, 8001, 8292, 9112, 6214, 7034, 7325,
  8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338,
  11158, 2122, 2942, 3233, 4053, 3498, 4318, 4609, 5429, 3759, 4579, 4870,
  5690, 5135, 5955, 6246, 7066, 4168, 4988, 5279, 6099, 5544, 6364, 6655,
  7475, 5805, 6625, 6916, 7736, 7181, 8001, 8292, 9112, 4168, 4988, 5279,
  6099, 5544, 6364, 6655, 7475, 5805, 6625, 6916, 7736, 7181, 8001, 8292,
  9112, 6214, 7034, 7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962,
  9782, 9227, 10047, 10338, 11158, 4168, 4988, 5279, 6099, 5544, 6364, 6655,
  7475, 5805, 6625, 6916, 7736, 7181, 8001, 8292, 9112, 6214, 7034, 7325,
  8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338,
  11158, 6214, 7034, 7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962,
  9782, 9227, 10047, 10338, 11158, 8260, 9080, 9371, 10191, 9636, 10456,
  10747, 11567, 9897, 10717, 11008, 11828, 11273, 12093, 12384, 13204, 2122,
  2942, 3233, 4053, 3498, 4318, 4609, 5429, 3759, 4579, 4870, 5690, 5135,
  5955, 6246, 7066, 4168, 4988, 5279, 6099, 5544, 6364, 6655, 7475, 5805,
  6625, 6916, 7736, 7181, 8001, 8292, 9112, 4168, 4988, 5279, 6099, 5544,
  6364, 6655, 7475, 5805, 6625, 6916, 7736, 7181, 8001, 8292, 9112, 6214,
  7034, 7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227,
  10047, 10338, 11158, 4168, 4988, 5279, 6099, 5544, 6364, 6655, 7475, 5805,
  6625, 6916, 7736, 7181, 8001, 8292, 9112, 6214, 7034, 7325, 8145, 7590,
  8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338, 11158, 6214,
  7034, 7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227,
  10047, 10338, 11158, 8260, 9080, 9371, 10191, 9636, 10456, 10747, 11567,
  9897, 10717, 11008, 11828, 11273, 12093, 12384, 13204, 4168, 4988, 5279,
  6099, 5544, 6364, 6655, 7475, 5805, 6625, 6916, 7736, 7181, 8001, 8292,
  9112, 6214, 7034, 7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962,
  9782, 9227, 10047, 10338, 11158, 6214, 7034, 7325, 8145, 7590, 8410, 8701,
  9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338, 11158, 8260, 9080, 9371,
  10191, 9636, 10456, 10747, 11567, 9897, 10717, 11008, 11828, 11273, 12093,
  12384, 13204, 6214, 7034, 7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671,
  8962, 9782, 9227, 10047, 10338, 11158, 8260, 9080, 9371, 10191, 9636, 10456,
  10747, 11567, 9897, 10717, 11008, 11828, 11273, 12093, 12384, 13204, 8260,
  9080, 9371, 10191, 9636, 10456, 10747, 11567, 9897, 10717, 11008, 11828,
  11273, 12093, 12384, 13204, 10306, 11126, 11417, 12237, 11682, 12502, 12793,
  13613, 11943, 12763, 13054, 13874, 13319, 14139, 14430, 15250, 2122, 2942,
  3233, 4053, 3498, 4318, 4609, 5429, 3759, 4579, 4870, 5690, 5135, 5955,
  6246, 7066, 4168, 4988, 5279, 6099, 5544, 6364, 6655, 7475, 5805, 6625,
  6916, 7736, 7181, 8001, 8292, 9112, 4168, 4988, 5279, 6099, 5544, 6364,
  6655, 7475, 5805, 6625, 6916, 7736, 7181, 8001, 8292, 9112, 6214, 7034,
  7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047,
  10338, 11158, 4168, 4988, 5279, 6099, 5544, 6364, 6655, 7475, 5805, 6625,
  6916, 7736, 7181, 8001, 8292, 9112, 6214, 7034, 7325, 8145, 7590, 8410,
  8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338, 11158, 6214, 7034,
  7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047,
  10338, 11158, 8260, 9080, 9371, 10191, 9636, 10456, 10747, 11567, 9897,
  10717, 11008, 11828, 11273, 12093, 12384, 13204, 4168, 4988, 5279, 6099,
  5544, 6364, 6655, 7475, 5805, 6625, 6916, 7736, 7181, 8001, 8292, 9112,
  6214, 7034, 7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962, 9782,
  9227, 10047, 10338, 11158, 6214, 7034, 7325, 8145, 7590, 8410, 8701, 9521,
  7851, 8671, 8962, 9782, 9227, 10047, 10338, 11158, 8260, 9080, 9371, 10191,
  9636, 10456, 10747, 11567, 9897, 10717, 11008, 11828, 11273, 12093, 12384,
  13204, 6214, 7034, 7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962,
  9782, 9227, 10047, 10338, 11158, 8260, 9080, 9371, 10191, 9636, 10456,
  10747, 11567, 9897, 10717, 11008, 11828, 11273, 12093, 12384, 13204, 8260,
  9080, 9371, 10191, 9636, 10456, 10747, 11567, 9897, 10717, 11008, 11828,
  11273, 12093, 12384, 13204, 10306, 11126, 11417, 12237, 11682, 12502, 12793,
  13613, 11943, 12763, 13054, 13874, 13319, 14139, 14430, 15250, 4168, 4988,
  5279, 6099, 5544, 6364, 6655, 7475, 5805, 6625, 6916, 7736, 7181, 8001,
  8292, 9112, 6214, 7034, 7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671,
  8962, 9782, 9227, 10047, 10338, 11158, 6214, 7034, 7325, 8145, 7590, 8410,
  8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338, 11158, 8260, 9080,
  9371, 10191, 9636, 10456, 10747, 11567, 9897, 10717, 11008, 11828, 11273,
  12093, 12384, 13204, 6214, 7034, 7325, 8145, 7590, 8410, 8701, 9521, 7851,
  8671, 8962, 9782, 9227, 10047, 10338, 11158, 8260, 9080, 9371, 10191, 9636,
  10456, 10747, 11567, 9897, 10717, 11008, 11828, 11273, 12093, 12384, 13204,
  8260, 9080, 9371, 10191, 9636, 10456, 10747, 11567, 9897, 10717, 11008,
  11828, 11273, 12093, 12384, 13204, 10306, 11126, 11417, 12237, 11682, 12502,
  12793, 13613, 11943, 12763, 13054, 13874, 13319, 14139, 14430, 15250, 6214,
  7034, 7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227,
  10047, 10338, 11158, 8260, 9080, 9371, 10191, 9636, 10456, 10747, 11567,
  9897, 10717, 11008, 11828, 11273, 12093, 12384, 13204, 8260, 9080, 9371,
  10191, 9636, 10456, 10747, 11567, 9897, 10717, 11008, 11828, 11273, 12093,
  12384, 13204, 10306, 11126, 11417, 12237, 11682, 12502, 12793, 13613, 11943,
  12763, 13054, 13874, 13319, 14139, 14430, 15250, 8260, 9080, 9371, 10191,
  9636, 10456, 10747, 11567, 9897, 10717, 11008, 11828, 11273, 12093, 12384,
  13204, 10306, 11126, 11417, 12237, 11682, 12502, 12793, 13613, 11943, 12763,
  13054, 13874, 13319, 14139, 14430, 15250, 10306, 11126, 11417, 12237, 11682,
  12502, 12793, 13613, 11943, 12763, 13054, 13874, 13319, 14139, 14430, 15250,
  12352, 13172, 13463, 14283, 13728, 14548, 14839, 15659, 13989, 14809, 15100,
  15920, 15365, 16185, 16476, 17296, 2122, 2942, 3233, 4053, 3498, 4318, 4609,
  5429, 3759, 4579, 4870, 5690, 5135, 5955, 6246, 7066, 4168, 4988, 5279,
  6099, 5544, 6364, 6655, 7475, 5805, 6625, 6916, 7736, 7181, 8001, 8292,
  9112, 4168, 4988, 5279, 6099, 5544, 6364, 6655, 7475, 5805, 6625, 6916,
  7736, 7181, 8001, 8292, 9112, 6214, 7034, 7325, 8145, 7590, 8410, 8701,
  9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338, 11158, 4168, 4988, 5279,
  6099, 5544, 6364, 6655, 7475, 5805, 6625, 6916, 7736, 7181, 8001, 8292,
  9112, 6214, 7034, 7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962,
  9782, 9227, 10047, 10338, 11158, 6214, 7034, 7325, 8145, 7590, 8410, 8701,
  9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338, 11158, 8260, 9080, 9371,
  10191, 9636, 10456, 10747, 11567, 9897, 10717, 11008, 11828, 11273, 12093,
  12384, 13204, 4168, 4988, 5279, 6099, 5544, 6364, 6655, 7475, 5805, 6625,
  6916, 7736, 7181, 8001, 8292, 9112, 6214, 7034, 7325, 8145, 7590, 8410,
  8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338, 11158, 6214, 7034,
  7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047,
  10338, 11158, 8260, 9080, 9371, 10191, 9636, 10456, 10747, 11567, 9897,
  10717, 11008, 11828, 11273, 12093, 12384, 13204, 6214, 7034, 7325, 8145,
  7590, 8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338, 11158,
  8260, 9080, 9371, 10191, 9636, 10456, 10747, 11567, 9897, 10717, 11008,
  11828, 11273, 12093, 12384, 13204, 8260, 9080, 9371, 10191, 9636, 10456,
  10747, 11567, 9897, 10717, 11008, 11828, 11273, 12093, 12384, 13204, 10306,
  11126, 11417, 12237, 11682, 12502, 12793, 13613, 11943, 12763, 13054, 13874,
  13319, 14139, 14430, 15250, 4168, 4988, 5279, 6099, 5544, 6364, 6655, 7475,
  5805, 6625, 6916, 7736, 7181, 8001, 8292, 9112, 6214, 7034, 7325, 8145,
  7590, 8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338, 11158,
  6214, 7034, 7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962, 9782,
  9227, 10047, 10338, 11158, 8260, 9080, 9371, 10191, 9636, 10456, 10747,
  11567, 9897, 10717, 11008, 11828, 11273, 12093, 12384, 13204, 6214, 7034,
  7325, 8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047,
  10338, 11158, 8260, 9080, 9371, 10191, 9636, 10456, 10747, 11567, 9897,
  10717, 11008, 11828, 11273, 12093, 12384, 13204, 8260, 9080, 9371, 10191,
  9636, 10456, 10747, 11567, 9897, 10717, 11008, 11828, 11273, 12093, 12384,
  13204, 10306, 11126, 11417, 12237, 11682, 12502, 12793, 13613, 11943, 12763,
  13054, 13874, 13319, 14139, 14430, 15250, 6214, 7034, 7325, 8145, 7590,
  8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338, 11158, 8260,
  9080, 9371, 10191, 9636, 10456, 10747, 11567, 9897, 10717, 11008, 11828,
  11273, 12093, 12384, 13204, 8260, 9080, 9371, 10191, 9636, 10456, 10747,
  11567, 9897, 10717, 11008, 11828, 11273, 12093, 12384, 13204, 10306, 11126,
  11417, 12237, 11682, 12502, 12793, 13613, 11943, 12763, 13054, 13874, 13319,
  14139, 14430, 15250, 8260, 9080, 9371, 10191, 9636, 10456, 10747, 11567,
  9897, 10717, 11008, 11828, 11273, 12093, 12384, 13204, 10306, 11126, 11417,
  12237, 11682, 12502, 12793, 13613, 11943, 12763, 13054, 13874, 13319, 14139,
  14430, 15250, 10306, 11126, 11417, 12237, 11682, 12502, 12793, 13613, 11943,
  12763, 13054, 13874, 13319, 14139, 14430, 15250, 12352, 13172, 13463, 14283,
  13728, 14548, 14839, 15659, 13989, 14809, 15100, 15920, 15365, 16185, 16476,
  17296, 4168, 4988, 5279, 6099, 5544, 6364, 6655, 7475, 5805, 6625, 6916,
  7736, 7181, 8001, 8292, 9112, 6214, 7034, 7325, 8145, 7590, 8410, 8701,
  9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338, 11158, 6214, 7034, 7325,
  8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338,
  11158, 8260, 9080, 9371, 10191, 9636, 10456, 10747, 11567, 9897, 10717,
  11008, 11828, 11273, 12093, 12384, 13204, 6214, 7034, 7325, 8145, 7590,
  8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338, 11158, 8260,
  9080, 9371, 10191, 9636, 10456, 10747, 11567, 9897, 10717, 11008, 11828,
  11273, 12093, 12384, 13204, 8260, 9080, 9371, 10191, 9636, 10456, 10747,
  11567, 9897, 10717, 11008, 11828, 11273, 12093, 12384, 13204, 10306, 11126,
  11417, 12237, 11682, 12502, 12793, 13613, 11943, 12763, 13054, 13874, 13319,
  14139, 14430, 15250, 6214, 7034, 7325, 8145, 7590, 8410, 8701, 9521, 7851,
  8671, 8962, 9782, 9227, 10047, 10338, 11158, 8260, 9080, 9371, 10191, 9636,
  10456, 10747, 11567, 9897, 10717, 11008, 11828, 11273, 12093, 12384, 13204,
  8260, 9080, 9371, 10191, 9636, 10456, 10747, 11567, 9897, 10717, 11008,
  11828, 11273, 12093, 12384, 13204, 10306, 11126, 11417, 12237, 11682, 12502,
  12793, 13613, 11943, 12763, 13054, 13874, 13319, 14139, 14430, 15250, 8260,
  9080, 9371, 10191, 9636, 10456, 10747, 11567, 9897, 10717, 11008, 11828,
  11273, 12093, 12384, 13204, 10306, 11126, 11417, 12237, 11682, 12502, 12793,
  13613, 11943, 12763, 13054, 13874, 13319, 14139, 14430, 15250, 10306, 11126,
  11417, 12237, 11682, 12502, 12793, 13613, 11943, 12763, 13054, 13874, 13319,
  14139, 14430, 15250, 12352, 13172, 13463, 14283, 13728, 14548, 14839, 15659,
  13989, 14809, 15100, 15920, 15365, 16185, 16476, 17296, 6214, 7034, 7325,
  8145, 7590, 8410, 8701, 9521, 7851, 8671, 8962, 9782, 9227, 10047, 10338,
  11158, 8260, 9080, 9371, 10191, 9636, 10456, 10747, 11567, 9897, 10717,
  11008, 11828, 11273, 12093, 12384, 13204, 8260, 9080, 9371, 10191, 9636,
  10456, 10747, 11567, 9897, 10717, 11008, 11828, 11273, 12093, 12384, 13204,
  10306, 11126, 11417, 12237, 11682, 12502, 12793, 13613, 11943, 12763, 13054,
  13874, 13319, 14139, 14430, 15250, 8260, 9080, 9371, 10191, 9636, 10456,
  10747, 11567, 9897, 10717, 11008, 11828, 11273, 12093, 12384, 13204, 10306,
  11126, 11417, 12237, 11682, 12502, 12793, 13613, 11943, 12763, 13054, 13874,
  13319, 14139, 14430, 15250, 10306, 11126, 11417, 12237, 11682, 12502, 12793,
  13613, 11943, 12763, 13054, 13874, 13319, 14139, 14430, 15250, 12352, 13172,
  13463, 14283, 13728, 14548, 14839, 15659, 13989, 14809, 15100, 15920, 15365,
  16185, 16476, 17296, 8260, 9080, 9371, 10191, 9636, 10456, 10747, 11567,
  9897, 10717, 11008, 11828, 11273, 12093, 12384, 13204, 10306, 11126, 11417,
  12237, 11682, 12502, 12793, 13613, 11943, 12763, 13054, 13874, 13319, 14139,
  14430, 15250, 10306, 11126, 11417, 12237, 11682, 12502, 12793, 13613, 11943,
  12763, 13054, 13874, 13319, 14139, 14430, 15250, 12352, 13172, 13463, 14283,
  13728, 14548, 14839, 15659, 13989, 14809, 15100, 15920, 15365, 16185, 16476,
  17296, 10306, 11126, 11417, 12237, 11682, 12502, 12793, 13613, 11943, 12763,
  13054, 13874, 13319, 14139, 14430, 15250, 12352, 13172, 13463, 14283, 13728,
  14548, 14839, 15659, 13989, 14809, 15100, 15920, 15365, 16185, 16476, 17296,
  12352, 13172, 13463, 14283, 13728, 14548, 14839, 15659, 13989, 14809, 15100,
  15920, 15365, 16185, 16476, 17296, 14398, 15218, 15509, 16329, 15774, 16594,
  16885, 17705, 16035, 16855, 17146, 17966, 17411, 18231, 18522, 19342
};
#endif

#if CONFIG_VP9_HIGHBITDEPTH
static const vp9_tree_index cat1_high10[2] = {0, 0};
static const vp9_tree_index cat2_high10[4] = {2, 2, 0, 0};
static const vp9_tree_index cat3_high10[6] = {2, 2, 4, 4, 0, 0};
static const vp9_tree_index cat4_high10[8] = {2, 2, 4, 4, 6, 6, 0, 0};
static const vp9_tree_index cat5_high10[10] = {2, 2, 4, 4, 6, 6, 8, 8, 0, 0};
static const vp9_tree_index cat6_high10[32] = {2, 2, 4, 4, 6, 6, 8, 8, 10, 10,
  12, 12, 14, 14, 16, 16, 18, 18, 20, 20, 22, 22, 24, 24, 26, 26, 28, 28,
  30, 30, 0, 0};
static const vp9_tree_index cat1_high12[2] = {0, 0};
static const vp9_tree_index cat2_high12[4] = {2, 2, 0, 0};
static const vp9_tree_index cat3_high12[6] = {2, 2, 4, 4, 0, 0};
static const vp9_tree_index cat4_high12[8] = {2, 2, 4, 4, 6, 6, 0, 0};
static const vp9_tree_index cat5_high12[10] = {2, 2, 4, 4, 6, 6, 8, 8, 0, 0};
static const vp9_tree_index cat6_high12[36] = {2, 2, 4, 4, 6, 6, 8, 8, 10, 10,
  12, 12, 14, 14, 16, 16, 18, 18, 20, 20, 22, 22, 24, 24, 26, 26, 28, 28,
  30, 30, 32, 32, 34, 34, 0, 0};
#endif

const vp9_extra_bit vp9_extra_bits[ENTROPY_TOKENS] = {
  {0, 0, 0, 0, zero_cost},                             // ZERO_TOKEN
  {0, 0, 0, 1, one_cost},                              // ONE_TOKEN
  {0, 0, 0, 2, two_cost},                              // TWO_TOKEN
  {0, 0, 0, 3, three_cost},                            // THREE_TOKEN
  {0, 0, 0, 4, four_cost},                             // FOUR_TOKEN
  {cat1, vp9_cat1_prob, 1,  CAT1_MIN_VAL, cat1_cost},  // CATEGORY1_TOKEN
  {cat2, vp9_cat2_prob, 2,  CAT2_MIN_VAL, cat2_cost},  // CATEGORY2_TOKEN
  {cat3, vp9_cat3_prob, 3,  CAT3_MIN_VAL, cat3_cost},  // CATEGORY3_TOKEN
  {cat4, vp9_cat4_prob, 4,  CAT4_MIN_VAL, cat4_cost},  // CATEGORY4_TOKEN
  {cat5, vp9_cat5_prob, 5,  CAT5_MIN_VAL, cat5_cost},  // CATEGORY5_TOKEN
  {cat6, vp9_cat6_prob, 14, CAT6_MIN_VAL, 0},          // CATEGORY6_TOKEN
  {0, 0, 0, 0, zero_cost}                              // EOB_TOKEN
};

#if CONFIG_VP9_HIGHBITDEPTH
const vp9_extra_bit vp9_extra_bits_high10[ENTROPY_TOKENS] = {
  {0, 0, 0, 0, zero_cost},                                           // ZERO
  {0, 0, 0, 1, one_cost},                                            // ONE
  {0, 0, 0, 2, two_cost},                                            // TWO
  {0, 0, 0, 3, three_cost},                                          // THREE
  {0, 0, 0, 4, four_cost},                                           // FOUR
  {cat1_high10, vp9_cat1_prob_high10, 1,  CAT1_MIN_VAL, cat1_cost},  // CAT1
  {cat2_high10, vp9_cat2_prob_high10, 2,  CAT2_MIN_VAL, cat2_cost},  // CAT2
  {cat3_high10, vp9_cat3_prob_high10, 3,  CAT3_MIN_VAL, cat3_cost},  // CAT3
  {cat4_high10, vp9_cat4_prob_high10, 4,  CAT4_MIN_VAL, cat4_cost},  // CAT4
  {cat5_high10, vp9_cat5_prob_high10, 5,  CAT5_MIN_VAL, cat5_cost},  // CAT5
  {cat6_high10, vp9_cat6_prob_high10, 16, CAT6_MIN_VAL, 0},          // CAT6
  {0, 0, 0, 0, zero_cost}                                            // EOB
};
const vp9_extra_bit vp9_extra_bits_high12[ENTROPY_TOKENS] = {
  {0, 0, 0, 0, zero_cost},                                           // ZERO
  {0, 0, 0, 1, one_cost},                                            // ONE
  {0, 0, 0, 2, two_cost},                                            // TWO
  {0, 0, 0, 3, three_cost},                                          // THREE
  {0, 0, 0, 4, four_cost},                                           // FOUR
  {cat1_high12, vp9_cat1_prob_high12, 1,  CAT1_MIN_VAL, cat1_cost},  // CAT1
  {cat2_high12, vp9_cat2_prob_high12, 2,  CAT2_MIN_VAL, cat2_cost},  // CAT2
  {cat3_high12, vp9_cat3_prob_high12, 3,  CAT3_MIN_VAL, cat3_cost},  // CAT3
  {cat4_high12, vp9_cat4_prob_high12, 4,  CAT4_MIN_VAL, cat4_cost},  // CAT4
  {cat5_high12, vp9_cat5_prob_high12, 5,  CAT5_MIN_VAL, cat5_cost},  // CAT5
  {cat6_high12, vp9_cat6_prob_high12, 18, CAT6_MIN_VAL, 0},          // CAT6
  {0, 0, 0, 0, zero_cost}                                            // EOB
};
#endif

const struct vp9_token vp9_coef_encodings[ENTROPY_TOKENS] = {
  {2, 2}, {6, 3}, {28, 5}, {58, 6}, {59, 6}, {60, 6}, {61, 6}, {124, 7},
  {125, 7}, {126, 7}, {127, 7}, {0, 1}
};


struct tokenize_b_args {
  VP9_COMP *cpi;
  ThreadData *td;
  TOKENEXTRA **tp;
};

static void set_entropy_context_b_inter(int plane, int block,
                                        BLOCK_SIZE plane_bsize,
                                        int blk_row, int blk_col,
                                        TX_SIZE tx_size, void *arg) {
  struct tokenize_b_args* const args = arg;
  ThreadData *const td = args->td;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  struct macroblock_plane *p = &x->plane[plane];
  struct macroblockd_plane *pd = &xd->plane[plane];
  vp9_set_contexts(xd, pd, plane_bsize, tx_size, p->eobs[block] > 0,
                   blk_col, blk_row);
}

static void set_entropy_context_b(int plane, int block, BLOCK_SIZE plane_bsize,
                                  TX_SIZE tx_size, void *arg) {
  struct tokenize_b_args* const args = arg;
  ThreadData *const td = args->td;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  struct macroblock_plane *p = &x->plane[plane];
  struct macroblockd_plane *pd = &xd->plane[plane];
  int aoff, loff;
  txfrm_block_to_raster_xy(plane_bsize, tx_size, block, &aoff, &loff);
  vp9_set_contexts(xd, pd, plane_bsize, tx_size, p->eobs[block] > 0,
                   aoff, loff);
}

static INLINE void add_token(TOKENEXTRA **t, const vp9_prob *context_tree,
                             int32_t extra, uint8_t token,
                             uint8_t skip_eob_node,
                             unsigned int *counts) {
  (*t)->token = token;
  (*t)->extra = extra;
  (*t)->context_tree = context_tree;
  (*t)->skip_eob_node = skip_eob_node;
  (*t)++;
  ++counts[token];
}

static INLINE void add_token_no_extra(TOKENEXTRA **t,
                                      const vp9_prob *context_tree,
                                      uint8_t token,
                                      uint8_t skip_eob_node,
                                      unsigned int *counts) {
  (*t)->token = token;
  (*t)->context_tree = context_tree;
  (*t)->skip_eob_node = skip_eob_node;
  (*t)++;
  ++counts[token];
}

static INLINE int get_tx_eob(const struct segmentation *seg, int segment_id,
                             TX_SIZE tx_size) {
  const int eob_max = 16 << (tx_size << 1);
  return vp9_segfeature_active(seg, segment_id, SEG_LVL_SKIP) ? 0 : eob_max;
}

static void tokenize_b_inter(int plane, int block, BLOCK_SIZE plane_bsize,
                             int blk_row, int blk_col,
                             TX_SIZE tx_size, void *arg) {
  struct tokenize_b_args* const args = arg;
  VP9_COMP *cpi = args->cpi;
  ThreadData *const td = args->td;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  TOKENEXTRA **tp = args->tp;
  uint8_t token_cache[32 * 32];
  struct macroblock_plane *p = &x->plane[plane];
  struct macroblockd_plane *pd = &xd->plane[plane];
  MB_MODE_INFO *mbmi = &xd->mi[0].src_mi->mbmi;
  int pt; /* near block/prev token context index */
  int c;
  TOKENEXTRA *t = *tp;        /* store tokens starting here */
  int eob = p->eobs[block];
  const PLANE_TYPE type = pd->plane_type;
  const tran_low_t *qcoeff = BLOCK_OFFSET(p->qcoeff, block);
  const int segment_id = mbmi->segment_id;
  const int16_t *scan, *nb;
  const scan_order *so;
  const int ref = is_inter_block(mbmi);
  unsigned int (*const counts)[COEFF_CONTEXTS][ENTROPY_TOKENS] =
      td->rd_counts.coef_counts[tx_size][type][ref];
  vp9_prob (*const coef_probs)[COEFF_CONTEXTS][UNCONSTRAINED_NODES] =
      cpi->common.fc->coef_probs[tx_size][type][ref];
  unsigned int (*const eob_branch)[COEFF_CONTEXTS] =
      td->counts->eob_branch[tx_size][type][ref];
  const uint8_t *const band = get_band_translate(tx_size);
  const int seg_eob = get_tx_eob(&cpi->common.seg, segment_id, tx_size);
  int16_t token;
  EXTRABIT extra;

  pt = get_entropy_context(tx_size, pd->above_context + blk_col,
                           pd->left_context + blk_row);
  so = get_scan(xd, tx_size, type, block);
  scan = so->scan;
  nb = so->neighbors;
  c = 0;

  while (c < eob) {
    int v = 0;
    int skip_eob = 0;
    v = qcoeff[scan[c]];

    while (!v) {
      add_token_no_extra(&t, coef_probs[band[c]][pt], ZERO_TOKEN, skip_eob,
                         counts[band[c]][pt]);
      eob_branch[band[c]][pt] += !skip_eob;

      skip_eob = 1;
      token_cache[scan[c]] = 0;
      ++c;
      pt = get_coef_context(nb, token_cache, c);
      v = qcoeff[scan[c]];
    }

    vp9_get_token_extra(v, &token, &extra);

    add_token(&t, coef_probs[band[c]][pt], extra, (uint8_t)token,
              (uint8_t)skip_eob, counts[band[c]][pt]);
    eob_branch[band[c]][pt] += !skip_eob;

    token_cache[scan[c]] = vp9_pt_energy_class[token];
    ++c;
    pt = get_coef_context(nb, token_cache, c);
  }
  if (c < seg_eob) {
    add_token_no_extra(&t, coef_probs[band[c]][pt], EOB_TOKEN, 0,
                       counts[band[c]][pt]);
    ++eob_branch[band[c]][pt];
  }

  *tp = t;

  vp9_set_contexts(xd, pd, plane_bsize, tx_size, c > 0, blk_col, blk_row);
}

static void tokenize_b(int plane, int block, BLOCK_SIZE plane_bsize,
                       TX_SIZE tx_size, void *arg) {
  struct tokenize_b_args* const args = arg;
  VP9_COMP *cpi = args->cpi;
  ThreadData *const td = args->td;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  TOKENEXTRA **tp = args->tp;
  uint8_t token_cache[32 * 32];
  struct macroblock_plane *p = &x->plane[plane];
  struct macroblockd_plane *pd = &xd->plane[plane];
  MB_MODE_INFO *mbmi = &xd->mi[0].src_mi->mbmi;
  int pt; /* near block/prev token context index */
  int c;
  TOKENEXTRA *t = *tp;        /* store tokens starting here */
  int eob = p->eobs[block];
  const PLANE_TYPE type = pd->plane_type;
  const tran_low_t *qcoeff = BLOCK_OFFSET(p->qcoeff, block);
  const int segment_id = mbmi->segment_id;
  const int16_t *scan, *nb;
  const scan_order *so;
  const int ref = is_inter_block(mbmi);
  unsigned int (*const counts)[COEFF_CONTEXTS][ENTROPY_TOKENS] =
      td->rd_counts.coef_counts[tx_size][type][ref];
  vp9_prob (*const coef_probs)[COEFF_CONTEXTS][UNCONSTRAINED_NODES] =
      cpi->common.fc->coef_probs[tx_size][type][ref];
  unsigned int (*const eob_branch)[COEFF_CONTEXTS] =
      td->counts->eob_branch[tx_size][type][ref];
  const uint8_t *const band = get_band_translate(tx_size);
  const int seg_eob = get_tx_eob(&cpi->common.seg, segment_id, tx_size);
  int16_t token;
  EXTRABIT extra;
  int aoff, loff;
  txfrm_block_to_raster_xy(plane_bsize, tx_size, block, &aoff, &loff);

  pt = get_entropy_context(tx_size, pd->above_context + aoff,
                           pd->left_context + loff);
  so = get_scan(xd, tx_size, type, block);
  scan = so->scan;
  nb = so->neighbors;
  c = 0;

  while (c < eob) {
    int v = 0;
    int skip_eob = 0;
    v = qcoeff[scan[c]];

    while (!v) {
      add_token_no_extra(&t, coef_probs[band[c]][pt], ZERO_TOKEN, skip_eob,
                         counts[band[c]][pt]);
      eob_branch[band[c]][pt] += !skip_eob;

      skip_eob = 1;
      token_cache[scan[c]] = 0;
      ++c;
      pt = get_coef_context(nb, token_cache, c);
      v = qcoeff[scan[c]];
    }

    vp9_get_token_extra(v, &token, &extra);

    add_token(&t, coef_probs[band[c]][pt], extra, (uint8_t)token,
              (uint8_t)skip_eob, counts[band[c]][pt]);
    eob_branch[band[c]][pt] += !skip_eob;

    token_cache[scan[c]] = vp9_pt_energy_class[token];
    ++c;
    pt = get_coef_context(nb, token_cache, c);
  }
  if (c < seg_eob) {
    add_token_no_extra(&t, coef_probs[band[c]][pt], EOB_TOKEN, 0,
                       counts[band[c]][pt]);
    ++eob_branch[band[c]][pt];
  }

  *tp = t;

  vp9_set_contexts(xd, pd, plane_bsize, tx_size, c > 0, aoff, loff);
}

struct is_skippable_args {
  MACROBLOCK *x;
  int *skippable;
};
static void is_skippable(int plane, int block,
                         BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
                         void *argv) {
  struct is_skippable_args *args = argv;
  (void)plane_bsize;
  (void)tx_size;
  args->skippable[0] &= (!args->x->plane[plane].eobs[block]);
}

// TODO(yaowu): rewrite and optimize this function to remove the usage of
//              vp9_foreach_transform_block() and simplify is_skippable().
int vp9_is_skippable_in_plane(MACROBLOCK *x, BLOCK_SIZE bsize, int plane) {
  int result = 1;
  struct is_skippable_args args = {x, &result};
  vp9_foreach_transformed_block_in_plane(&x->e_mbd, bsize, plane, is_skippable,
                                         &args);
  return result;
}

static void has_high_freq_coeff(int plane, int block,
                                BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
                                void *argv) {
  struct is_skippable_args *args = argv;
  int eobs = (tx_size == TX_4X4) ? 3 : 10;
  (void) plane_bsize;

  *(args->skippable) |= (args->x->plane[plane].eobs[block] > eobs);
}

int vp9_has_high_freq_in_plane(MACROBLOCK *x, BLOCK_SIZE bsize, int plane) {
  int result = 0;
  struct is_skippable_args args = {x, &result};
  vp9_foreach_transformed_block_in_plane(&x->e_mbd, bsize, plane,
                                         has_high_freq_coeff, &args);
  return result;
}

void tokenize_tx(VP9_COMP *cpi, ThreadData *td, TOKENEXTRA **t,
                 int dry_run, TX_SIZE tx_size, BLOCK_SIZE plane_bsize,
                 int blk_row, int blk_col, int block, int plane,
                 void *arg) {
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0].src_mi->mbmi;
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  int blk_idx = (blk_row / 2) * 8 + (blk_col / 2);
  TX_SIZE plane_tx_size = plane ?
      get_uv_tx_size_impl(mbmi->inter_tx_size[blk_idx],
                          plane_bsize, 0, 0) :
      mbmi->inter_tx_size[blk_idx];

  int max_blocks_high = num_4x4_blocks_high_lookup[plane_bsize];
  int max_blocks_wide = num_4x4_blocks_wide_lookup[plane_bsize];

  if (xd->mb_to_bottom_edge < 0)
    max_blocks_high += xd->mb_to_bottom_edge >> (5 + pd->subsampling_y);
  if (xd->mb_to_right_edge < 0)
    max_blocks_wide += xd->mb_to_right_edge >> (5 + pd->subsampling_x);

  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide)
    return;

  if (tx_size == plane_tx_size) {
    if (!dry_run)
      tokenize_b_inter(plane, block, plane_bsize,
                       blk_row, blk_col, tx_size, arg);
    else
      set_entropy_context_b_inter(plane, block, plane_bsize,
                                  blk_row, blk_col, tx_size, arg);
  } else {
    BLOCK_SIZE bsize = txsize_to_bsize[tx_size];
    int bh = num_4x4_blocks_wide_lookup[bsize];
    int i;

    assert(num_4x4_blocks_high_lookup[bsize] ==
           num_4x4_blocks_wide_lookup[bsize]);

    for (i = 0; i < 4; ++i) {
      int offsetr = (i >> 1) * bh / 2;
      int offsetc = (i & 0x01) * bh / 2;
      int step = 1 << (2 *(tx_size - 1));
      tokenize_tx(cpi, td, t, dry_run, tx_size - 1, plane_bsize,
                  blk_row + offsetr, blk_col + offsetc,
                  block + i * step, plane, arg);
    }
  }
}

void vp9_tokenize_sb_inter(VP9_COMP *cpi, ThreadData *td, TOKENEXTRA **t,
                           int dry_run, BLOCK_SIZE bsize) {
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0].src_mi->mbmi;
  TOKENEXTRA *t_backup = *t;
  const int ctx = vp9_get_skip_context(xd);
  const int skip_inc = !vp9_segfeature_active(&cm->seg, mbmi->segment_id,
                                              SEG_LVL_SKIP);
  struct tokenize_b_args arg = {cpi, td, t};
  int plane;

  if (mbmi->skip) {
    if (!dry_run)
      td->counts->skip[ctx][1] += skip_inc;
    reset_skip_context(xd, bsize);
    if (dry_run)
      *t = t_backup;
    return;
  }

  if (!dry_run)
    td->counts->skip[ctx][0] += skip_inc;
  else
    *t = t_backup;

  for (plane = 0; plane < MAX_MB_PLANE; ++plane) {
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    const BLOCK_SIZE plane_bsize = get_plane_block_size(bsize, pd);
    const int mi_width = num_4x4_blocks_wide_lookup[plane_bsize];
    const int mi_height = num_4x4_blocks_high_lookup[plane_bsize];
    BLOCK_SIZE txb_size = txsize_to_bsize[max_txsize_lookup[plane_bsize]];
    int bh = num_4x4_blocks_wide_lookup[txb_size];
    int idx, idy;
    int block = 0;
    int step = 1 << (max_txsize_lookup[plane_bsize] * 2);

    for (idy = 0; idy < mi_height; idy += bh) {
      for (idx = 0; idx < mi_width; idx += bh) {
        tokenize_tx(cpi, td, t, dry_run, max_txsize_lookup[plane_bsize],
                    plane_bsize, idy, idx, block, plane, &arg);
        block += step;
      }
    }
  }
}

void vp9_tokenize_sb(VP9_COMP *cpi, ThreadData *td, TOKENEXTRA **t,
                     int dry_run, BLOCK_SIZE bsize) {
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0].src_mi->mbmi;
  TOKENEXTRA *t_backup = *t;
  const int ctx = vp9_get_skip_context(xd);
  const int skip_inc = !vp9_segfeature_active(&cm->seg, mbmi->segment_id,
                                              SEG_LVL_SKIP);
  struct tokenize_b_args arg = {cpi, td, t};
  if (mbmi->skip) {
    if (!dry_run)
      td->counts->skip[ctx][1] += skip_inc;
    reset_skip_context(xd, bsize);
    if (dry_run)
      *t = t_backup;
    return;
  }

  if (!dry_run) {
    td->counts->skip[ctx][0] += skip_inc;
    vp9_foreach_transformed_block(xd, bsize, tokenize_b, &arg);
  } else {
    vp9_foreach_transformed_block(xd, bsize, set_entropy_context_b, &arg);
    *t = t_backup;
  }
}
