#include <xlnt/xlnt.hpp>

#include "interface/storage/writer.hpp"

#include "importer/importer.hpp"

void Importer::import_data(const std::string& file_path)
{
    xlnt::workbook wb;
    wb.load(file_path);
    auto ws = wb.active_sheet();
    for (auto row : ws.rows(false))
    {
        for (auto cell : row)
        {
            // Process each cell
            std::cout << cell.to_string() << " ";
        }
        std::cout << std::endl;
    }
}