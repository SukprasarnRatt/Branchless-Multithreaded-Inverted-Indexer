#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numa.h>
#include <numaif.h>
#include <queue>
#include <sched.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#include "index.hpp"
#include "tokenizer.hpp"

using namespace std;
namespace fs = std::filesystem;

std::mutex cout_mutex;

struct FileData {
    string filePath;
    vector<char*> content;
    size_t size;
};

static vector<pair<string, uintmax_t>> crawlDataset(const string& path) {
    vector<pair<string, uintmax_t>> fileInfos;

    try {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            try {
                uintmax_t fileSize = entry.file_size();
                fileInfos.emplace_back(entry.path().string(), fileSize);
            } catch (const fs::filesystem_error& e) {
                cerr << "Error getting size of file: " << entry.path()
                     << " - " << e.what() << endl;
            }
        }
    } catch (const std::exception& e) {
        cerr << "Filesystem error: " << e.what() << endl;
    }

    return fileInfos;
}

static void setThreadAffinityToNode(int threadId, int nodeId, const string& label) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    struct bitmask* cpumask = numa_bitmask_alloc(numa_num_possible_cpus());

    if (cpumask == nullptr) {
        lock_guard<mutex> guard(cout_mutex);
        cerr << "Error allocating NUMA bitmask for " << label << " " << threadId << endl;
        return;
    }

    if (numa_node_to_cpus(nodeId, cpumask) != 0) {
        lock_guard<mutex> guard(cout_mutex);
        cerr << "Error retrieving CPUs for node " << nodeId << endl;
        numa_bitmask_free(cpumask);
        return;
    }

    for (int i = 0; i < cpumask->size; ++i) {
        if (numa_bitmask_isbitset(cpumask, i)) {
            CPU_SET(i, &cpuset);
        }
    }

    numa_bitmask_free(cpumask);

    int ret = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    {
        lock_guard<mutex> guard(cout_mutex);
        if (ret != 0) {
            cerr << "Error setting thread affinity for " << label << " "
                 << threadId << " on node " << nodeId << endl;
        } else {
            cout << label << " " << threadId
                 << " affinity set to Node " << nodeId << endl;
        }
    }

    int current_cpu = sched_getcpu();
    int current_node = numa_node_of_cpu(current_cpu);

    {
        lock_guard<mutex> guard(cout_mutex);
        cout << label << " " << threadId
             << " is running on CPU " << current_cpu
             << " (Node " << current_node << ")" << endl;
    }
}

static void loadFilesOnNode(
    int threadId,
    int nodeId,
    const vector<pair<string, uintmax_t>>& files,
    queue<FileData>& fileBuffer,
    mutex& bufferMutex,
    bool affinityFlag
) {
    if (affinityFlag) {
        setThreadAffinityToNode(threadId, nodeId, "Loader Thread");
    }

    for (const auto& [filePath, fileSize] : files) {
        if (filePath.empty() || filePath.find("/.") != string::npos || fileSize == 0) {
            continue;
        }

        int fd = open(filePath.c_str(), O_RDONLY);
        if (fd == -1) {
            lock_guard<mutex> guard(cout_mutex);
            cerr << "Loader Thread " << threadId
                 << " - Error opening file: " << filePath << endl;
            continue;
        }

        char* buffer = new char[fileSize + 1];
        ssize_t bytesRead = read(fd, buffer, fileSize);

        if (bytesRead == static_cast<ssize_t>(fileSize)) {
            buffer[fileSize] = '\0';
            posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
            close(fd);

            vector<char*> bufferVector;
            bufferVector.push_back(buffer);

            {
                lock_guard<mutex> lock(bufferMutex);
                fileBuffer.push({filePath, std::move(bufferVector), static_cast<size_t>(fileSize)});
            }
        } else {
            lock_guard<mutex> guard(cout_mutex);
            cerr << "Loader Thread " << threadId
                 << " - Error reading file: " << filePath << endl;

            delete[] buffer;
            close(fd);
        }
    }

    lock_guard<mutex> guard(cout_mutex);
    cout << "Loader Thread " << threadId
         << " completed loading files on Node " << nodeId << endl;
}

static void processFiles(
    int threadId,
    vector<queue<FileData>>& fileBuffersPerNode,
    vector<mutex>& bufferMutexes,
    mutex& tokenMutex,
    mutex& bytesMutex,
    char charDict[256],
    uintmax_t& totalBytes,
    uintmax_t& totalTokens,
    vector<double>& tokenizationTimes,
    vector<uintmax_t>& bytesProcessed,
    bool affinityFlag,
    std::atomic<uint32_t>& globalNextDocId,
    InvertedIndex& localIndex
) {
    int totalNodes = numa_max_node() + 1;
    if (totalNodes <= 0) {
        totalNodes = 1;
    }

    int node = (threadId - 1) % totalNodes;

    if (affinityFlag) {
        setThreadAffinityToNode(threadId, node, "Thread");
    }

    double threadTokenizationTime = 0.0;

    while (true) {
        FileData fileData;
        bool foundWork = false;

        {
            lock_guard<mutex> lock(bufferMutexes[node]);
            if (!fileBuffersPerNode[node].empty()) {
                fileData = std::move(fileBuffersPerNode[node].front());
                fileBuffersPerNode[node].pop();
                foundWork = true;
            }
        }

        if (!foundWork) {
            break;
        }

        uintmax_t fileSize = fileData.size;

        {
            lock_guard<mutex> lock(bytesMutex);
            totalBytes += fileSize;
        }

        bytesProcessed[threadId - 1] += fileSize;

        char* buffer = fileData.content[0];

        auto tokenStart = chrono::high_resolution_clock::now();
        vector<char*> tokens = tokenize(buffer, fileSize, charDict);
        auto tokenEnd = chrono::high_resolution_clock::now();

        chrono::duration<double> tokenDuration = tokenEnd - tokenStart;
        threadTokenizationTime += tokenDuration.count();

        {
            lock_guard<mutex> lock(tokenMutex);
            totalTokens += tokens.size();
        }

        unordered_map<string, uint32_t> termFreqs;
        termFreqs.reserve(tokens.size());

        for (char* token : tokens) {
            if (token == nullptr || *token == '\0') {
                continue;
            }
            // convert token pointer into a real C++ string
            string term(token);

            // lowercase the token
            for (char& c : term) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }

            termFreqs[term]++;
        }
        // assign a unique document ID
        uint32_t docId = globalNextDocId.fetch_add(1, std::memory_order_relaxed);
        // store document length
        uint32_t docLength = static_cast<uint32_t>(tokens.size());

        // store document metadata
        localIndex.docStore[docId] = fileData.filePath;
        localIndex.docLengths[docId] = docLength;

        // convert term-frequency table into postings
        for (const auto& [term, freq] : termFreqs) {
            localIndex.index[term].push_back({docId, freq});
        }

        delete[] buffer;
        tokenizationTimes[threadId - 1] = threadTokenizationTime;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 5) {
        cerr << "Usage: ./tokenizer <input_directory> [num_threads] [affinity_flag] [output_file]" << endl;
        cerr << "Example: ./tokenizer /path/to/data 16 1 index.bin" << endl;
        return 1;
    }

    string directoryPath = argv[1];

    if (!fs::exists(directoryPath) || !fs::is_directory(directoryPath)) {
        cerr << "Error: invalid directory path " << directoryPath << endl;
        return 1;
    }

    int numThreads = static_cast<int>(thread::hardware_concurrency());
    if (numThreads <= 0) {
        numThreads = 1;
    }

    if (argc >= 3) {
        numThreads = stoi(argv[2]);
        if (numThreads <= 0) {
            cerr << "Error: num_threads must be > 0" << endl;
            return 1;
        }
    }

    bool affinityFlag = false;
    if (argc >= 4) {
        affinityFlag = (stoi(argv[3]) != 0);
    }

    string outputFile = "index.bin";
    if (argc >= 5) {
        outputFile = argv[4];
    }

    cout << "Starting indexFiles with path: " << directoryPath << endl;
    cout << "Output index file: " << outputFile << endl;

    uintmax_t totalBytes = 0;
    uintmax_t totalTokens = 0;

    vector<pair<string, uintmax_t>> fileInfos = crawlDataset(directoryPath);
    cout << "Crawled dataset. Number of files: " << fileInfos.size() << endl;

    sort(fileInfos.begin(), fileInfos.end(),
         [](const auto& a, const auto& b) { return a.second > b.second; });

    int totalNodes = numa_max_node() + 1;
    if (totalNodes <= 0) {
        cerr << "NUMA is not available or could not determine the number of nodes." << endl;
        totalNodes = 1;
    }
    cout << "Total NUMA nodes detected: " << totalNodes << endl;

    vector<vector<pair<string, uintmax_t>>> filesPerNode(totalNodes);
    for (size_t i = 0; i < fileInfos.size(); ++i) {
        int node = static_cast<int>(i % totalNodes);
        filesPerNode[node].push_back(fileInfos[i]);
    }

    vector<queue<FileData>> fileBuffersPerNode(totalNodes);
    vector<mutex> bufferMutexes(totalNodes);

    vector<thread> loaderThreads;
    for (int node = 0; node < totalNodes; ++node) {
        loaderThreads.emplace_back(
            loadFilesOnNode,
            node + 1,
            node,
            cref(filesPerNode[node]),
            ref(fileBuffersPerNode[node]),
            ref(bufferMutexes[node]),
            affinityFlag
        );
    }

    for (auto& t : loaderThreads) {
        if (t.joinable()) {
            t.join();
        }
    }

    size_t totalFilesLoaded = 0;
    for (const auto& q : fileBuffersPerNode) {
        totalFilesLoaded += q.size();
    }
    cout << "All loader threads have completed. Total files loaded: "
         << totalFilesLoaded << endl;

    char charDict[256];
    loadDictionaryAlphaNumeric(charDict);

    mutex tokenMutex, bytesMutex;
    vector<double> tokenizationTimes(numThreads, 0.0);
    vector<uintmax_t> bytesProcessed(numThreads, 0);

    vector<InvertedIndex> localIndexes(numThreads);
    std::atomic<uint32_t> globalNextDocId{0};

    auto totalStart = chrono::high_resolution_clock::now();

    vector<thread> processingThreads;
    for (int i = 0; i < numThreads; ++i) {
        processingThreads.emplace_back(
            processFiles,
            i + 1,
            ref(fileBuffersPerNode),
            ref(bufferMutexes),
            ref(tokenMutex),
            ref(bytesMutex),
            charDict,
            ref(totalBytes),
            ref(totalTokens),
            ref(tokenizationTimes),
            ref(bytesProcessed),
            affinityFlag,
            ref(globalNextDocId),
            ref(localIndexes[i])
        );
    }

    for (auto& t : processingThreads) {
        if (t.joinable()) {
            t.join();
        }
    }

    auto totalEnd = chrono::high_resolution_clock::now();
    chrono::duration<double> totalDuration = totalEnd - totalStart;
    double totalTime = totalDuration.count();

    InvertedIndex finalIndex;
    for (int i = 0; i < numThreads; ++i) {
        mergeIntoFinalIndex(finalIndex, localIndexes[i]);
    }

    int longestThreadId = 0;
    double longestTime = 0.0;

    for (int i = 0; i < numThreads; ++i) {
        if (tokenizationTimes[i] > longestTime) {
            longestTime = tokenizationTimes[i];
            longestThreadId = i + 1;
        }
    }

    cout << fixed << setprecision(4);

    for (int i = 0; i < numThreads; ++i) {
        cout << "Thread " << (i + 1)
             << " tokenization time: " << tokenizationTimes[i]
             << " seconds" << endl;

        cout << "Thread " << (i + 1)
             << " processed " << bytesProcessed[i]
             << " bytes" << endl;
    }

    cout << "Thread " << longestThreadId
         << " took the longest time for tokenization: "
         << longestTime << " seconds" << endl;

    cout << "Total execution time (create and join threads): "
         << totalTime << " seconds" << endl;

    uintmax_t totalProcessedBytes = 0;
    for (int i = 0; i < numThreads; ++i) {
        totalProcessedBytes += bytesProcessed[i];
    }

    cout << "Completed indexing " << totalProcessedBytes << " bytes of data" << endl;
    cout << "Completed indexing " << totalTokens << " tokens" << endl;
    cout << "Unique terms in index: " << finalIndex.index.size() << endl;
    cout << "Documents in index: " << finalIndex.docStore.size() << endl;
    cout << "Stored document lengths: " << finalIndex.docLengths.size() << endl;

    double throughput_MB_per_s = 0.0;
    if (totalTime > 0.0) {
        throughput_MB_per_s =
            (static_cast<double>(totalProcessedBytes) / (1024.0 * 1024.0)) / totalTime;
    }

    cout << "Average Throughput: " << throughput_MB_per_s << " MB/s" << endl;

    saveIndex(finalIndex, outputFile);

    return 0;
}