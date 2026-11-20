#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_plugin.h"

int flat_native_plugin_init(FlatNativePluginRegisterFn reg) {
    if (!reg) {
        return -1;
    }
    reg(FLAT_NATIVE_CSV_READER, flat_native_csv_reader);
    reg(FLAT_NATIVE_CSV_WRITER, flat_native_csv_writer);
    reg(FLAT_NATIVE_CSV_CLOSE, flat_native_csv_close);
    reg(FLAT_NATIVE_CSV_RESET, flat_native_csv_reset);
    reg(FLAT_NATIVE_CSV_ISEOF, flat_native_csv_is_eof);
    reg(FLAT_NATIVE_CSV_ROWS, flat_native_csv_rows);
    reg(FLAT_NATIVE_CSV_COLS, flat_native_csv_cols);
    reg(FLAT_NATIVE_CSV_GET_ROW, flat_native_csv_get_row);
    reg(FLAT_NATIVE_CSV_GET_COL, flat_native_csv_get_col);
    reg(FLAT_NATIVE_CSV_SET_ROW, flat_native_csv_set_row);
    reg(FLAT_NATIVE_CSV_NEXT, flat_native_csv_next);
    reg(FLAT_NATIVE_CSV_NEXT_CELLS, flat_native_csv_next_cells);
    reg(FLAT_NATIVE_CSV_SET_SEPARATOR, flat_native_csv_set_separator);
    reg(FLAT_NATIVE_CSV_SET_LINE_ENDINGS, flat_native_csv_set_line_endings);
    reg(FLAT_NATIVE_CSV_SET_ENCODING, flat_native_csv_set_encoding);
    reg(FLAT_NATIVE_CSV_SET_QUOTE, flat_native_csv_set_quote);
    reg(FLAT_NATIVE_CSV_WRITE_ROWS, flat_native_csv_write_rows);
    reg(FLAT_NATIVE_CSV_WRITE_COLS, flat_native_csv_write_cols);
    reg(FLAT_NATIVE_CSV_FLUSH, flat_native_csv_flush);
    reg(FLAT_NATIVE_CSV_GET_CELLS, flat_native_csv_get_cells);
    reg(FLAT_NATIVE_CSV_VIEW, flat_native_csv_view);
    reg(FLAT_NATIVE_CSV_CLOSE_VIEW, flat_native_csv_close_view);
    reg(FLAT_NATIVE_CSV_SET_CELL, flat_native_csv_set_cell);
    reg(FLAT_NATIVE_CSV_GET_CELL, flat_native_csv_get_cell);
    reg(FLAT_NATIVE_CSV_GET_VIEW_ROW, flat_native_csv_get_view_row);
    return 0;
}