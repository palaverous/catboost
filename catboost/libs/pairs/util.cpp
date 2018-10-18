#include "util.h"

#include <catboost/libs/helpers/permutation.h>
#include <catboost/libs/options/loss_description.h>
#include <util/generic/vector.h>
#include <util/generic/set.h>
#include <util/generic/hash.h>

static void GenerateBruteForce(
    int groupBegin,
    int groupEnd,
    int maxPairCount,
    int pairCount,
    const TVector<float>& targetId,
    TRestorableFastRng64* rand,
    TVector<TPair>* result
) {
    auto resultShift = result->size();
    for (int firstIdx = groupBegin; firstIdx < groupEnd; ++firstIdx) {
        for (int secondIdx = firstIdx + 1; secondIdx < groupEnd; ++secondIdx) {
            if (targetId[firstIdx] == targetId[secondIdx]) {
                continue;
            }
            if (targetId[firstIdx] > targetId[secondIdx]) {
                result->push_back(TPair(firstIdx, secondIdx, 1));
            } else {
                result->push_back(TPair(secondIdx, firstIdx, 1));
            }
        }
    }
    if (maxPairCount != NCatboostOptions::MAX_AUTOGENERATED_PAIRS_COUNT && maxPairCount < pairCount) {
        Shuffle(result->begin() + resultShift, result->end(), *rand);
        result->resize(resultShift + maxPairCount);
    }
}

static bool TryGeneratePair(
    int groupBegin,
    int groupEnd,
    const TVector<float>& targetId,
    TRestorableFastRng64* rand,
    int* firstIdxPtr,
    int* secondIdxPtr
) {
    int& firstIdx = *firstIdxPtr;
    int& secondIdx = *secondIdxPtr;
    auto size = groupEnd - groupBegin;
    firstIdx = rand->Uniform(size) + groupBegin;
    secondIdx = rand->Uniform(size) + groupBegin;
    if (firstIdx == secondIdx || targetId[firstIdx] == targetId[secondIdx]) {
        return false;
    }
    if (targetId[firstIdx] < targetId[secondIdx]) {
        std::swap(firstIdx, secondIdx);
    }
    return true;
}

static void GenerateRandomly(
    int groupBegin,
    int groupEnd,
    int maxPairCount,
    const TVector<float>& targetId,
    TRestorableFastRng64* rand,
    TVector<TPair>* result
) {
    TSet<std::pair<int, int>> generatedPairs;
    while (int(generatedPairs.size()) < maxPairCount) {
        int firstIdx, secondIdx;
        if (TryGeneratePair(groupBegin, groupEnd, targetId, rand, &firstIdx, &secondIdx) && !generatedPairs.has(std::make_pair(firstIdx, secondIdx))) {
            generatedPairs.insert({firstIdx, secondIdx});
        }
    }
    for (auto& pair : generatedPairs) {
        result->push_back(TPair(pair.first, pair.second, 1));
    }
}

void GeneratePairLogitPairs(
    const TVector<TGroupId>& groupId,
    const TVector<float>& targetId,
    int maxPairCount,
    TRestorableFastRng64* rand,
    TVector<TPair>* result
) {
    CB_ENSURE(!targetId.empty(), "Pool labels are not provided. Cannot generate pairs.");
    THashMap<float, int> targetCount;
    targetCount[targetId[0]] = 1;
    for (int docIdx = 1, groupBeginIdx = 0; docIdx <= groupId.ysize(); ++docIdx) {
        if (docIdx == groupId.ysize() || groupId[docIdx] != groupId[groupBeginIdx]) {
            int pairCount = 0;
            for (auto target: targetCount) {
                pairCount += target.second * (docIdx - groupBeginIdx - target.second);
            }
            pairCount /= 2;
            if (maxPairCount == NCatboostOptions::MAX_AUTOGENERATED_PAIRS_COUNT || pairCount / 2 < maxPairCount) {
                GenerateBruteForce(groupBeginIdx, docIdx, maxPairCount, pairCount, targetId, rand, result);
            } else {
                GenerateRandomly(groupBeginIdx, docIdx, maxPairCount, targetId, rand, result);
            }
            targetCount.clear();
            groupBeginIdx = docIdx;
        }
        if (docIdx < targetId.ysize()) {
            targetCount[targetId[docIdx]]++;
        }
    }
}

TVector<TPair> GeneratePairLogitPairs(
    const TVector<TGroupId>& groupId,
    const TVector<float>& targetId,
    int maxPairCount,
    ui64 seed
) {
    TVector<TPair> pairs;
    TRestorableFastRng64 rand(seed);
    GeneratePairLogitPairs(groupId, targetId, maxPairCount, &rand, &pairs);
    return pairs;
}
