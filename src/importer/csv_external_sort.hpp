#pragma once

#include <string>
#include <cstdint>

namespace euleraph {

/**
 * External sort for CSV files to optimize WiredTiger write performance.
 * Sorts by (startId, edgeLabel, endId) columns.
 *
 * @param input_file   Path to input CSV file
 * @param output_file  Path to output sorted CSV file
 * @param chunk_size   Number of rows per chunk in memory (default: 500000)
 * @return             true on success, false on failure
 */
bool csv_external_sort(const std::string& input_file,
                       const std::string& output_file,
                       size_t chunk_size = 500000);

/**
 * Check if a sorted version of the file exists and is up-to-date.
 * If not, perform external sort and return path to sorted file.
 *
 * @param original_file  Path to original CSV file
 * @return               Path to sorted file (either existing or newly created)
 */
std::string ensure_sorted_csv(const std::string& original_file);

} // namespace euleraph
