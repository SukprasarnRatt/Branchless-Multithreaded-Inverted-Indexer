#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "index.hpp"

using namespace std;

struct SearchResult {
    uint32_t docId;
    uint32_t termFreq;
    uint32_t docLength;
    double score;
    string filePath;
};

static string normalizeTerm(string term) {
    for (char& c : term) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    return term;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: ./search <index_file> <term>" << endl;
        cerr << "Example: ./search index.bin cat" << endl;
        return 1;
    }

    string indexFile = argv[1];
    string queryTerm = normalizeTerm(argv[2]);

    InvertedIndex idx;
    if (!loadIndex(idx, indexFile)) {
        cerr << "Failed to load index." << endl;
        return 1;
    }

    const vector<Posting>* postings = searchTerm(idx, queryTerm);

    cout << "Term: " << queryTerm << endl;

    if (postings == nullptr || postings->empty()) {
        cout << "Found in 0 documents." << endl;
        return 0;
    }

    const uint32_t totalDocs = static_cast<uint32_t>(idx.docStore.size());
    const uint32_t df = static_cast<uint32_t>(postings->size());
    const double avgDocLen = computeAverageDocLength(idx);

    vector<SearchResult> results;
    results.reserve(postings->size());

    for (const Posting& posting : *postings) {
        string filePath = "(unknown path)";
        auto docIt = idx.docStore.find(posting.docId);
        if (docIt != idx.docStore.end()) {
            filePath = docIt->second;
        }

        uint32_t docLength = 0;
        auto lenIt = idx.docLengths.find(posting.docId);
        if (lenIt != idx.docLengths.end()) {
            docLength = lenIt->second;
        }

        double score = computeBM25(
            posting.termFreq,
            df,
            totalDocs,
            docLength,
            avgDocLen
        );

        results.push_back({
            posting.docId,
            posting.termFreq,
            docLength,
            score,
            filePath
        });
    }

    sort(results.begin(), results.end(),
         [](const SearchResult& a, const SearchResult& b) {
             if (a.score != b.score) {
                 return a.score > b.score;
             }
             return a.docId < b.docId;
         });

    cout << "Found in " << results.size() << " documents:" << endl;
    cout << fixed << setprecision(4);

    for (const auto& result : results) {
        cout << "docId " << result.docId
             << " -> " << result.filePath
             << " (freq=" << result.termFreq
             << ", docLen=" << result.docLength
             << ", score=" << result.score << ")" << endl;
    }

    return 0;
}