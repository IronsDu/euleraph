#include "importer/csv_external_sort.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "utils/csv.h"

namespace fs = std::filesystem;

namespace euleraph {

namespace {

// CSV row data for sorting
struct CsvRow {
    std::string start_id;
    std::string start_label;
    std::string edge_label;
    std::string end_id;
    std::string end_label;
};

// Comparator for sorting CsvRow by (start_id, edge_label, end_id)
bool compare_rows(const CsvRow& a, const CsvRow& b) {
    if (a.start_id != b.start_id) return a.start_id < b.start_id;
    if (a.edge_label != b.edge_label) return a.edge_label < b.edge_label;
    return a.end_id < b.end_id;
}

// Comparison key for k-way merge
struct SortKey {
    std::string start_id;
    std::string edge_label;
    std::string end_id;

    bool operator>(const SortKey& other) const {
        if (start_id != other.start_id) return start_id > other.start_id;
        if (edge_label != other.edge_label) return edge_label > other.edge_label;
        return end_id > other.end_id;
    }
};

// Get sort key from CSV row (for merge heap)
SortKey get_sort_key(const CsvRow& row) {
    return {row.start_id, row.edge_label, row.end_id};
}

// Write CSV row to output stream
void write_csv_row(std::ofstream& out, const CsvRow& row) {
    out << row.start_id << ','
        << row.start_label << ','
        << row.edge_label << ','
        << row.end_id << ','
        << row.end_label << '\n';
}

// Merge entry for k-way merge
struct MergeEntry {
    SortKey key;
    CsvRow row;
    int chunk_index;

    bool operator>(const MergeEntry& other) const {
        return key > other.key;  // min-heap, so reverse comparison
    }
};

} // anonymous namespace

bool csv_external_sort(const std::string& input_file,
                       const std::string& output_file,
                       size_t chunk_size) {
    spdlog::info("Starting external sort for CSV file: {}", input_file);

    io::CSVReader<5> csv_reader(input_file);
    csv_reader.read_header(io::ignore_extra_column, "startId", "startLabel", "edgeLabel", "endId", "endLabel");

    // Temporary files for sorted chunks
    std::vector<std::string> temp_files;
    std::vector<CsvRow> chunk;
    chunk.reserve(chunk_size);

    size_t total_rows = 0;
    size_t chunk_count = 0;

    // Phase 1: Read, sort, and write chunks
    spdlog::info("Phase 1: Splitting and sorting chunks...");

    std::string startId, startLabel, edgeLabel, endId, endLabel;
    while (csv_reader.read_row(startId, startLabel, edgeLabel, endId, endLabel)) {
        CsvRow row;
        row.start_id = std::move(startId);
        row.start_label = std::move(startLabel);
        row.edge_label = std::move(edgeLabel);
        row.end_id = std::move(endId);
        row.end_label = std::move(endLabel);

        chunk.push_back(std::move(row));
        total_rows++;

        if (chunk.size() >= chunk_size) {
            // Sort this chunk
            std::sort(chunk.begin(), chunk.end(), compare_rows);

            // Write to temp file
            std::string temp_path = output_file + ".chunk_" + std::to_string(chunk_count);
            std::ofstream temp_out(temp_path);
            if (!temp_out.is_open()) {
                spdlog::error("Failed to create temp file: {}", temp_path);
                return false;
            }

            for (const auto& r : chunk) {
                write_csv_row(temp_out, r);
            }
            temp_out.close();

            temp_files.push_back(temp_path);
            chunk_count++;
            spdlog::info("  Chunk {} written, {} rows", chunk_count, chunk.size());

            chunk.clear();
            chunk.reserve(chunk_size);
        }
    }

    // Handle remaining rows
    if (!chunk.empty()) {
        std::sort(chunk.begin(), chunk.end(), compare_rows);

        std::string temp_path = output_file + ".chunk_" + std::to_string(chunk_count);
        std::ofstream temp_out(temp_path);
        if (!temp_out.is_open()) {
            spdlog::error("Failed to create temp file: {}", temp_path);
            return false;
        }

        for (const auto& r : chunk) {
            write_csv_row(temp_out, r);
        }
        temp_out.close();

        temp_files.push_back(temp_path);
        chunk_count++;
        spdlog::info("  Chunk {} written, {} rows", chunk_count, chunk.size());
    }

    spdlog::info("Phase 1 complete: {} chunks, {} total rows", chunk_count, total_rows);

    // Phase 2: K-way merge
    spdlog::info("Phase 2: Merging sorted chunks...");

    // Open all chunk files and create CSV readers
    std::vector<std::unique_ptr<io::CSVReader<5>>> chunk_readers;
    for (const auto& temp_path : temp_files) {
        auto reader = std::make_unique<io::CSVReader<5>>(temp_path);
        // No header in temp files, read directly
        chunk_readers.push_back(std::move(reader));
    }

    // Priority queue for k-way merge (min-heap)
    std::priority_queue<MergeEntry, std::vector<MergeEntry>, std::greater<MergeEntry>> min_heap;

    // Initialize heap with first row from each chunk
    for (size_t i = 0; i < chunk_readers.size(); i++) {
        std::string sId, sLabel, eLabel, eId, eLabel2;
        if (chunk_readers[i]->read_row(sId, sLabel, eLabel, eId, eLabel2)) {
            CsvRow row{sId, sLabel, eLabel, eId, eLabel2};
            MergeEntry entry{get_sort_key(row), row, static_cast<int>(i)};
            min_heap.push(entry);
        }
    }

    // Write output
    std::ofstream out(output_file);
    if (!out.is_open()) {
        spdlog::error("Failed to create output file: {}", output_file);
        return false;
    }

    // Write header
    out << "startId,startLabel,edgeLabel,endId,endLabel\n";

    size_t merged_rows = 0;
    while (!min_heap.empty()) {
        MergeEntry entry = min_heap.top();
        min_heap.pop();

        write_csv_row(out, entry.row);
        merged_rows++;

        // Read next row from the same chunk
        std::string sId, sLabel, eLabel, eId, eLabel2;
        if (chunk_readers[entry.chunk_index]->read_row(sId, sLabel, eLabel, eId, eLabel2)) {
            CsvRow row{sId, sLabel, eLabel, eId, eLabel2};
            MergeEntry new_entry{get_sort_key(row), row, entry.chunk_index};
            min_heap.push(new_entry);
        }

        if (merged_rows % 100000 == 0) {
            spdlog::info("  Merged {} rows...", merged_rows);
        }
    }

    out.close();

    // Delete temp files
    for (const auto& temp_path : temp_files) {
        std::remove(temp_path.c_str());
    }

    spdlog::info("External sort complete: {} rows written to {}", merged_rows, output_file);
    return true;
}

std::string ensure_sorted_csv(const std::string& original_file) {
    fs::path orig_path(original_file);
    fs::path sorted_path = orig_path.parent_path() / (orig_path.stem().string() + "_sorted" + orig_path.extension().string());
    std::string sorted_file = sorted_path.string();

    // Check if sorted file already exists and is newer than original
    if (fs::exists(sorted_file)) {
        auto orig_time = fs::last_write_time(original_file);
        auto sorted_time = fs::last_write_time(sorted_file);
        if (sorted_time >= orig_time) {
            spdlog::info("Using existing sorted file: {}", sorted_file);
            return sorted_file;
        }
    }

    // Perform external sort
    spdlog::info("Sorted file not found or outdated, performing external sort...");
    if (!csv_external_sort(original_file, sorted_file)) {
        spdlog::error("External sort failed, falling back to original file");
        return original_file;
    }

    return sorted_file;
}

} // namespace euleraph
