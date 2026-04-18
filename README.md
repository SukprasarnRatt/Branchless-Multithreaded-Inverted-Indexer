# Branchless-Multithreaded-Inverted-Indexer

A high-performance C++ inverted indexing and search system for large-scale text collections on multicore NUMA machines. This project combines branchless tokenization, multithreaded indexing, NUMA-aware thread placement, a custom binary inverted index format, and BM25-based ranked retrieval.

## Overview

This project reads text files from a directory, tokenizes them using a branchless lookup-table-based tokenizer, builds an inverted index in parallel, stores the index in a custom binary format, and provides a separate search executable to query terms and rank matching documents using BM25.

The system is designed to explore performance-oriented information retrieval techniques in C++, including:

- branchless tokenization
- multithreaded indexing
- NUMA-aware thread affinity
- thread-local indexing with final merge
- custom binary index serialization
- BM25 document ranking

## Features

- **Branchless tokenizer**
  - Uses a 256-entry lookup table to classify characters as alphanumeric or delimiter
  - Replaces delimiters with `'\0'`
  - Returns pointers to token starts without constructing new strings

- **Multithreaded indexing**
  - Processes files in parallel using multiple worker threads
  - Supports thread placement across NUMA nodes
  - Stores term frequencies, document paths, and document lengths

- **NUMA-aware execution**
  - Detects available NUMA nodes
  - Pins loader and worker threads to specific nodes
  - Useful for evaluating memory locality and scaling behavior

- **Custom binary inverted index**
  - Saves the full term dictionary and postings lists to disk
  - Stores document metadata for later search and ranking
  - Reloads the index without rebuilding from raw documents

- **BM25 ranked search**
  - Searches for a term in the saved index
  - Computes BM25 scores using:
    - term frequency
    - document frequency
    - document length
    - average document length
  - Sorts results by score

## Project Structure

```text
Branchless-Multithreaded-Inverted-Indexer/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── index.hpp
│   └── tokenizer.hpp
├── src/
│   ├── main.cpp
│   ├── search.cpp
│   └── tokenizer.cpp
└── build/
```

## How It Works

The program runs in two major stages:

### 1. tokenizer
This executable builds the index.

Main responsibilities:

- crawl a directory of text files
- load and process files in parallel
- tokenize text using a branchless lookup-table tokenizer
- count term frequencies per document
- build an inverted index
- store document paths and document lengths
- save the final index to a binary file

### 2. search
This executable queries the saved index.

Main responsibilities: 
- load the binary inverted index from disk
- normalize the query term to lowercase
- retrieve the postings list for the term
- compute BM25 scores
- rank and print matching documents

## Inverted Index Design

The core data structure stores:

- term -> postings list
- docId -> file path
- docId -> document length

Each posting contains:
- docId
- termFreq

This allows the search program to:
- locate documents containing a term
- know how many times the term appears in each document
- use document length information for BM25 scoring

## Branchless Tokenization Strategy

The tokenizer uses a 256-entry lookup table:

- alphanumeric characters are marked as valid token characters
- non-alphanumeric characters are treated as delimiters

During tokenization:

- each byte is masked using the lookup table
- delimiters become '\0'
- token starts are detected when the current character is   valid and the previous one is a delimiter

This keeps the inner loop simple and avoids repeated branching on character classification.

# Build Requirements

- C++17 or later
- Linux recommended
- CMake 3.10+
- pthread support
- NUMA library (libnuma)

On Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y build-essential cmake libnuma-dev
```

## Build

Example using CMake:

```bash
mkdir build
cd build
cmake ..
make
```

This produces two executables:
- tokenizer
- search

## Usage
Build the index


```bash
./tokenizer <dataset_path> <num_threads> <Node-Affinity-Flag> <output_index_file>
```
Example:

```bash
./tokenizer /home/cc/Data/folder1/ 4 1 index.bin
```
Search the index:

```bash
./search <index_file> <term>
```

Example:
```bash
./search index.bin transport
```


## Example Run

### Indexing output

Example:
```bash
Starting indexFiles with path: /home/cc/Data/folder1/
Output index file: index.bin
Crawled dataset. Number of files: 49
Total NUMA nodes detected: 2
Loader Thread 2 affinity set to Node 1
Loader Thread 2 is running on CPU 81 (Node 1)
Loader Thread 1 affinity set to Node 0
Loader Thread 1 is running on CPU 38 (Node 0)
Loader Thread 2 completed loading files on Node 1
Loader Thread 1 completed loading files on Node 0
All loader threads have completed. Total files loaded: 49
Thread 1 affinity set to Node 0
Thread 1 is running on CPU 38 (Node 0)
Thread 2 affinity set to Node 1
Thread 2 is running on CPU 79 (Node 1)
Thread 4 affinity set to Node 1
Thread 3 affinity set to Node 0
Thread 3 is running on CPU 60 (Node 0)
Thread 4 is running on CPU 5 (Node 1)
Thread 1 tokenization time: 0.0157 seconds
Thread 1 processed 5051443 bytes
Thread 2 tokenization time: 0.0200 seconds
Thread 2 processed 4052503 bytes
Thread 3 tokenization time: 0.0173 seconds
Thread 3 processed 3550343 bytes
Thread 4 tokenization time: 0.0179 seconds
Thread 4 processed 4194817 bytes
Thread 2 took the longest time for tokenization: 0.0200 seconds
Total execution time (create and join threads): 0.0789 seconds
Completed indexing 16849106 bytes of data
Completed indexing 3010636 tokens
Unique terms in index: 66759
Documents in index: 49
Stored document lengths: 49
Average Throughput: 203.7666 MB/s
Index saved to: index.bin
Total terms in index: 66759
Total documents in index: 49
Stored document lengths: 49
```



### Search output

- **CPU:** Intel Xeon Gold 6240R @ 2.40 GHz
- **Architecture:** x86_64
- **Sockets:** 2
- **Cores per socket:** 24
- **Threads per core:** 2
- **Total logical CPUs:** 96
- **NUMA nodes:** 2

```bash
Term: transport
Found in 6 documents:
docId 48 -> /home/cc/Data/folder1/Document10010.txt (freq=2, docLen=10726, score=3.6535)
docId 13 -> /home/cc/Data/folder1/Document10094.txt (freq=5, docLen=85292, score=3.4267)
docId 34 -> /home/cc/Data/folder1/Document10051.txt (freq=1, docLen=45536, score=2.2819)
docId 24 -> /home/cc/Data/folder1/Document10044.txt (freq=1, docLen=48693, score=2.2295)
docId 10 -> /home/cc/Data/folder1/Document10009.txt (freq=1, docLen=91170, score=1.7031)
docId 6 -> /home/cc/Data/folder1/Document10060.txt (freq=1, docLen=99923, score=1.6241)
```

## Interpreting Search Results
Each result line shows:

- docId: internal document identifier
- file path: original file location
- freq: number of times the term appears in the document
- docLen: total number of tokens in that document
- score: BM25 relevance score

A document with a lower raw frequency can still rank higher if the term is more concentrated in a shorter document. That is expected BM25 behavior.

## BM25 Ranking

The search executable ranks documents using BM25.

BM25 uses:

- term frequency in the document
- document frequency across the collection
- total number of documents
- document length
- average document length

This gives more meaningful rankings than sorting only by raw term frequency.

## Binary Index Format

The custom binary index file stores:

1. term dictionary
2. postings lists for each term
3. document store (docId -> file path)
4. document lengths (docId -> token count)

This lets the search program load the index directly without reprocessing the original text files.

## Performance Notes

This project is designed for performance experiments on multicore systems.

Current optimizations include:

- -O3
- -march=native
- branchless tokenization
- multithreading
- NUMA-aware thread affinity

The indexing output reports:

- per-thread tokenization time
- per-thread bytes processed
- total bytes indexed
- total tokens indexed
- vocabulary size
- throughput in MB/s

## Limitations
- The tokenizer is currently byte-based and optimized for alphanumeric ASCII-style tokenization
- UTF-8 multibyte characters such as accented letters may not be handled correctly
- Current search supports single-term queries
- The system is a research-oriented prototype, not yet a full production search engine


## Future Improvements

Possible extensions:

- UTF-8 aware tokenization
- phrase queries
- multi-term queries
- stopword filtering
- stemming
- compressed postings lists
- positional index support
- top-k retrieval optimization
- distributed indexing and search across multiple nodes


## Research Motivation

This project explores how low-level systems techniques can improve information retrieval performance. Instead of relying on general-purpose tokenization libraries, it uses a branchless character classification strategy to reduce overhead in the tokenization stage while still supporting indexing and ranked retrieval.

## License

This project is provided for research and educational use.

## Author

Sukprasan Rattanapani