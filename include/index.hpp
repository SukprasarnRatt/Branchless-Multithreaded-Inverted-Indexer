#pragma once

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct Posting {
    uint32_t docId;
    uint32_t termFreq;
};

struct InvertedIndex {
    // term -> postings list
    std::unordered_map<std::string, std::vector<Posting>> index;

    // docId -> file path
    std::unordered_map<uint32_t, std::string> docStore;

    // docId -> document length in tokens
    std::unordered_map<uint32_t, uint32_t> docLengths;
};

inline void mergeIntoFinalIndex(InvertedIndex& finalIndex, InvertedIndex& localIndex) {
    // Merge document ID -> file path mappings.
    for (auto& [docId, path] : localIndex.docStore) {
        finalIndex.docStore.emplace(docId, std::move(path));
    }

    // Merge document lengths.
    for (auto& [docId, length] : localIndex.docLengths) {
        finalIndex.docLengths.emplace(docId, length);
    }

    // Merge postings lists.
    for (auto& [term, postings] : localIndex.index) {
        auto& finalPostings = finalIndex.index[term];
        finalPostings.insert(
            finalPostings.end(),
            std::make_move_iterator(postings.begin()),
            std::make_move_iterator(postings.end())
        );
    }
}

inline void saveIndex(const InvertedIndex& idx, const std::string& outputPath) {
    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        std::cerr << "Error opening index output file: " << outputPath << std::endl;
        return;
    }

    // Write term dictionary and postings.
    uint32_t numTerms = static_cast<uint32_t>(idx.index.size());
    out.write(reinterpret_cast<const char*>(&numTerms), sizeof(numTerms));

    for (const auto& [term, postings] : idx.index) {
        uint32_t termLen = static_cast<uint32_t>(term.size());
        out.write(reinterpret_cast<const char*>(&termLen), sizeof(termLen));
        out.write(term.data(), static_cast<std::streamsize>(termLen));

        uint32_t numPostings = static_cast<uint32_t>(postings.size());
        out.write(reinterpret_cast<const char*>(&numPostings), sizeof(numPostings));

        for (const Posting& posting : postings) {
            out.write(reinterpret_cast<const char*>(&posting.docId), sizeof(posting.docId));
            out.write(reinterpret_cast<const char*>(&posting.termFreq), sizeof(posting.termFreq));
        }
    }

    // Write document store.
    uint32_t numDocs = static_cast<uint32_t>(idx.docStore.size());
    out.write(reinterpret_cast<const char*>(&numDocs), sizeof(numDocs));

    for (const auto& [docId, path] : idx.docStore) {
        out.write(reinterpret_cast<const char*>(&docId), sizeof(docId));

        uint32_t pathLen = static_cast<uint32_t>(path.size());
        out.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
        out.write(path.data(), static_cast<std::streamsize>(pathLen));
    }

    // Write document lengths.
    uint32_t numDocLengths = static_cast<uint32_t>(idx.docLengths.size());
    out.write(reinterpret_cast<const char*>(&numDocLengths), sizeof(numDocLengths));

    for (const auto& [docId, docLen] : idx.docLengths) {
        out.write(reinterpret_cast<const char*>(&docId), sizeof(docId));
        out.write(reinterpret_cast<const char*>(&docLen), sizeof(docLen));
    }

    if (!out) {
        std::cerr << "Error occurred while writing index to: " << outputPath << std::endl;
        return;
    }

    std::cout << "Index saved to: " << outputPath << std::endl;
    std::cout << "Total terms in index: " << numTerms << std::endl;
    std::cout << "Total documents in index: " << numDocs << std::endl;
    std::cout << "Stored document lengths: " << numDocLengths << std::endl;
}

inline bool loadIndex(InvertedIndex& idx, const std::string& inputPath) {
    std::ifstream in(inputPath, std::ios::binary);
    if (!in) {
        std::cerr << "Error opening index input file: " << inputPath << std::endl;
        return false;
    }

    idx.index.clear();
    idx.docStore.clear();
    idx.docLengths.clear();

    // Read term dictionary and postings.
    uint32_t numTerms = 0;
    in.read(reinterpret_cast<char*>(&numTerms), sizeof(numTerms));
    if (!in) {
        std::cerr << "Error reading number of terms from index file." << std::endl;
        return false;
    }

    for (uint32_t i = 0; i < numTerms; ++i) {
        uint32_t termLen = 0;
        in.read(reinterpret_cast<char*>(&termLen), sizeof(termLen));
        if (!in) {
            std::cerr << "Error reading term length." << std::endl;
            return false;
        }

        std::string term(termLen, '\0');
        in.read(term.data(), static_cast<std::streamsize>(termLen));
        if (!in) {
            std::cerr << "Error reading term bytes." << std::endl;
            return false;
        }

        uint32_t numPostings = 0;
        in.read(reinterpret_cast<char*>(&numPostings), sizeof(numPostings));
        if (!in) {
            std::cerr << "Error reading number of postings." << std::endl;
            return false;
        }

        std::vector<Posting> postings;
        postings.reserve(numPostings);

        for (uint32_t j = 0; j < numPostings; ++j) {
            Posting posting{};
            in.read(reinterpret_cast<char*>(&posting.docId), sizeof(posting.docId));
            in.read(reinterpret_cast<char*>(&posting.termFreq), sizeof(posting.termFreq));

            if (!in) {
                std::cerr << "Error reading posting data." << std::endl;
                return false;
            }

            postings.push_back(posting);
        }

        idx.index.emplace(std::move(term), std::move(postings));
    }

    // Read document store.
    uint32_t numDocs = 0;
    in.read(reinterpret_cast<char*>(&numDocs), sizeof(numDocs));
    if (!in) {
        std::cerr << "Error reading number of documents." << std::endl;
        return false;
    }

    for (uint32_t i = 0; i < numDocs; ++i) {
        uint32_t docId = 0;
        in.read(reinterpret_cast<char*>(&docId), sizeof(docId));
        if (!in) {
            std::cerr << "Error reading docId." << std::endl;
            return false;
        }

        uint32_t pathLen = 0;
        in.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
        if (!in) {
            std::cerr << "Error reading path length." << std::endl;
            return false;
        }

        std::string path(pathLen, '\0');
        in.read(path.data(), static_cast<std::streamsize>(pathLen));
        if (!in) {
            std::cerr << "Error reading document path." << std::endl;
            return false;
        }

        idx.docStore.emplace(docId, std::move(path));
    }

    // Read document lengths.
    uint32_t numDocLengths = 0;
    in.read(reinterpret_cast<char*>(&numDocLengths), sizeof(numDocLengths));
    if (!in) {
        std::cerr << "Error reading number of document lengths." << std::endl;
        return false;
    }

    for (uint32_t i = 0; i < numDocLengths; ++i) {
        uint32_t docId = 0;
        uint32_t docLen = 0;

        in.read(reinterpret_cast<char*>(&docId), sizeof(docId));
        in.read(reinterpret_cast<char*>(&docLen), sizeof(docLen));

        if (!in) {
            std::cerr << "Error reading document length data." << std::endl;
            return false;
        }

        idx.docLengths.emplace(docId, docLen);
    }

    return true;
}

inline const std::vector<Posting>* searchTerm(const InvertedIndex& idx, const std::string& term) {
    auto it = idx.index.find(term);
    if (it == idx.index.end()) {
        return nullptr;
    }
    return &it->second;
}

inline double computeAverageDocLength(const InvertedIndex& idx) {
    if (idx.docLengths.empty()) {
        return 0.0;
    }

    double totalLength = 0.0;
    for (const auto& [docId, docLen] : idx.docLengths) {
        (void)docId;
        totalLength += static_cast<double>(docLen);
    }

    return totalLength / static_cast<double>(idx.docLengths.size());
}

inline double computeBM25(
    uint32_t tf,
    uint32_t df,
    uint32_t totalDocs,
    uint32_t docLen,
    double avgDocLen,
    double k1 = 1.2,
    double b = 0.75
) {
    if (totalDocs == 0 || df == 0 || avgDocLen <= 0.0) {
        return 0.0;
    }

    const double tf_d = static_cast<double>(tf);
    const double df_d = static_cast<double>(df);
    const double totalDocs_d = static_cast<double>(totalDocs);
    const double docLen_d = static_cast<double>(docLen);

    const double idf = std::log(1.0 + ((totalDocs_d - df_d + 0.5) / (df_d + 0.5)));
    const double denom = tf_d + k1 * (1.0 - b + b * (docLen_d / avgDocLen));

    return idf * ((tf_d * (k1 + 1.0)) / denom);
}