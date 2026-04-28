#include "result_store.h"

#include <algorithm>

ResultStore::~ResultStore() {
    clear();
}

void ResultStore::clear() {
    for (ResultData& result : results_) {
        if (result.thumbnail) {
            g_object_unref(result.thumbnail);
            result.thumbnail = nullptr;
        }
    }
    results_.clear();
}

const ResultData& ResultStore::add(const ResultData& result) {
    results_.push_back(result);
    return results_.back();
}

bool ResultStore::removeByFilename(const std::string& filename, bool* removedWasBlurry) {
    bool removed = false;
    auto newEnd = std::remove_if(results_.begin(), results_.end(),
                                 [&filename, &removed, removedWasBlurry](ResultData& result) {
                                     if (result.filename != filename) {
                                         return false;
                                     }

                                     if (!removed && removedWasBlurry) {
                                         *removedWasBlurry = result.isBlurry;
                                     }
                                     removed = true;
                                     if (result.thumbnail) {
                                         g_object_unref(result.thumbnail);
                                         result.thumbnail = nullptr;
                                     }
                                     return true;
                                 });
    results_.erase(newEnd, results_.end());
    return removed;
}

int ResultStore::countSharp() const {
    int count = 0;
    for (const ResultData& result : results_) {
        if (!result.isBlurry) {
            ++count;
        }
    }
    return count;
}

int ResultStore::countBlurry() const {
    int count = 0;
    for (const ResultData& result : results_) {
        if (result.isBlurry) {
            ++count;
        }
    }
    return count;
}

std::vector<std::string> ResultStore::blurryFilenames() const {
    std::vector<std::string> filenames;
    for (const ResultData& result : results_) {
        if (result.isBlurry) {
            filenames.push_back(result.filename);
        }
    }
    return filenames;
}

std::vector<const ResultData*> ResultStore::visible(SortMode mode) const {
    std::vector<const ResultData*> visibleResults;
    visibleResults.reserve(results_.size());

    for (const ResultData& result : results_) {
        if (visibleForSort(result, mode)) {
            visibleResults.push_back(&result);
        }
    }

    if (mode == SortMode::SharpFirst || mode == SortMode::BlurryFirst) {
        const bool blurryFirst = mode == SortMode::BlurryFirst;
        std::stable_sort(visibleResults.begin(), visibleResults.end(), [blurryFirst](const ResultData* left, const ResultData* right) {
            if (left->isBlurry == right->isBlurry) {
                return false;
            }

            return blurryFirst ? left->isBlurry : !left->isBlurry;
        });
    }

    return visibleResults;
}

int ResultStore::visibleIndexForFilename(SortMode mode, const std::string& filename) const {
    const std::vector<const ResultData*> visibleResults = visible(mode);
    for (size_t i = 0; i < visibleResults.size(); ++i) {
        if (visibleResults[i]->filename == filename) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

const ResultData* ResultStore::visibleAt(SortMode mode, int index) const {
    if (index < 0) {
        return nullptr;
    }

    const std::vector<const ResultData*> visibleResults = visible(mode);
    if (static_cast<size_t>(index) >= visibleResults.size()) {
        return nullptr;
    }

    return visibleResults[static_cast<size_t>(index)];
}

bool ResultStore::visibleForSort(const ResultData& result, SortMode mode) {
    if (mode == SortMode::SharpOnly) {
        return !result.isBlurry;
    }

    if (mode == SortMode::BlurryOnly) {
        return result.isBlurry;
    }

    return true;
}
