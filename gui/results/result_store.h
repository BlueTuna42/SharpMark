#ifndef RESULT_STORE_H
#define RESULT_STORE_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string>
#include <vector>

struct ResultData {
    std::string filename;
    bool isBlurry;
    GdkPixbuf *thumbnail = nullptr;
};

enum class SortMode {
    Default = 0,
    SharpFirst,
    BlurryFirst,
    SharpOnly,
    BlurryOnly
};

class ResultStore {
public:
    ~ResultStore();

    void clear();
    const ResultData& add(const ResultData& result);
    bool removeByFilename(const std::string& filename, bool* removedWasBlurry = nullptr);

    int countSharp() const;
    int countBlurry() const;
    std::vector<std::string> blurryFilenames() const;
    std::vector<const ResultData*> visible(SortMode mode) const;
    int visibleIndexForFilename(SortMode mode, const std::string& filename) const;
    const ResultData* visibleAt(SortMode mode, int index) const;

private:
    static bool visibleForSort(const ResultData& result, SortMode mode);
    std::vector<ResultData> results_;
};

#endif
