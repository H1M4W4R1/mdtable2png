#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <olectl.h>
#include <gdiplus.h>
#endif

static const std::size_t MAX_INPUT_SIZE = 4u * 1024u * 1024u;
static const int CELL_PADDING_X = 12;
static const int CELL_PADDING_Y = 9;
static const int FONT_SIZE = 16;
static const int MIN_COLUMN_WIDTH = 96;
static const int MAX_COLUMN_WIDTH = 420;
static const int ROW_HEIGHT = 42;
static const char* VERSION_TEXT = "itable 0.1.0";

struct style_t {
    bool bold;
    bool italic;
    std::string color;

    style_t() : bold(false), italic(false), color("#1f2328")
    {
    }
};

struct text_run_t {
    std::string text;
    style_t style;
};

struct cell_t {
    std::string raw;
    std::vector<text_run_t> runs;
    std::size_t plain_length;

    cell_t() : plain_length(0u)
    {
    }
};

struct row_t {
    std::vector<cell_t> cells;
};

struct table_t {
    std::vector<row_t> rows;
    std::size_t column_count;

    table_t() : column_count(0u)
    {
    }
};

struct options_t {
    std::string input_path;
    std::string png_path;
    std::string svg_path;
    bool show_help;
    bool show_version;

    options_t() : show_help(false), show_version(false)
    {
    }
};

struct svg_rect_t {
    int x;
    int y;
    int width;
    int height;
    std::string fill;
    std::string stroke;
};

struct svg_text_t {
    int x;
    int y;
    int size;
    bool bold;
    bool italic;
    std::string fill;
    std::string text;
};

static std::string trim(const std::string& value)
{
    std::size_t first = 0u;
    std::size_t last = value.size();

    while (first < last && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        first++;
    }
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1u])) != 0) {
        last--;
    }

    return value.substr(first, last - first);
}

static std::string lower_copy(const std::string& value)
{
    std::string result = value;
    std::size_t index = 0u;

    for (index = 0u; index < result.size(); index++) {
        result[index] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[index])));
    }

    return result;
}

static bool starts_with_at(const std::string& value, std::size_t position, const std::string& token)
{
    return position + token.size() <= value.size() && value.compare(position, token.size(), token) == 0;
}

static bool is_hex_digit_string(const std::string& value)
{
    std::size_t index = 0u;

    for (index = 0u; index < value.size(); index++) {
        if (std::isxdigit(static_cast<unsigned char>(value[index])) == 0) {
            return false;
        }
    }

    return true;
}

static std::string normalize_color(const std::string& value)
{
    static const char* names[] = {
        "black", "white", "red", "green", "blue", "yellow", "purple", "orange",
        "gray", "grey", "cyan", "magenta", "pink", "brown", "navy", "teal"
    };
    std::string color = lower_copy(trim(value));
    std::size_t index = 0u;

    if (color.size() == 4u && color[0] == '#' && is_hex_digit_string(color.substr(1u))) {
        std::string expanded = "#";
        expanded += color[1];
        expanded += color[1];
        expanded += color[2];
        expanded += color[2];
        expanded += color[3];
        expanded += color[3];
        return expanded;
    }

    if (color.size() == 7u && color[0] == '#' && is_hex_digit_string(color.substr(1u))) {
        return color;
    }

    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        if (color == names[index]) {
            return color;
        }
    }

    return "";
}

static std::string xml_escape(const std::string& value)
{
    std::string result;
    std::size_t index = 0u;

    for (index = 0u; index < value.size(); index++) {
        const char ch = value[index];

        if (ch == '&') {
            result += "&amp;";
        } else if (ch == '<') {
            result += "&lt;";
        } else if (ch == '>') {
            result += "&gt;";
        } else if (ch == '"') {
            result += "&quot;";
        } else {
            result += ch;
        }
    }

    return result;
}

static std::string xml_unescape(const std::string& value)
{
    std::string result;
    std::size_t index = 0u;

    while (index < value.size()) {
        if (starts_with_at(value, index, "&amp;")) {
            result += '&';
            index += 5u;
        } else if (starts_with_at(value, index, "&lt;")) {
            result += '<';
            index += 4u;
        } else if (starts_with_at(value, index, "&gt;")) {
            result += '>';
            index += 4u;
        } else if (starts_with_at(value, index, "&quot;")) {
            result += '"';
            index += 6u;
        } else {
            result += value[index];
            index++;
        }
    }

    return result;
}

static void flush_run(std::vector<text_run_t>* runs, std::string* pending, const style_t& style)
{
    text_run_t run;

    if (pending->empty()) {
        return;
    }

    run.text = *pending;
    run.style = style;
    runs->push_back(run);
    pending->clear();
}

static bool extract_span_color(const std::string& tag, std::string* color)
{
    std::string lowered = lower_copy(tag);
    std::size_t color_pos = lowered.find("color");
    std::size_t value_start = std::string::npos;
    std::size_t value_end = std::string::npos;

    if (color_pos == std::string::npos) {
        return false;
    }

    value_start = lowered.find(':', color_pos);
    if (value_start == std::string::npos) {
        value_start = lowered.find('=', color_pos);
    }
    if (value_start == std::string::npos) {
        return false;
    }

    value_start++;
    while (value_start < tag.size()) {
        const char ch = tag[value_start];
        if (std::isspace(static_cast<unsigned char>(ch)) == 0 && ch != '\'' && ch != '"') {
            break;
        }
        value_start++;
    }

    value_end = value_start;
    while (value_end < tag.size()) {
        const char ch = tag[value_end];
        if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == ';' || ch == '\'' || ch == '"') {
            break;
        }
        value_end++;
    }

    *color = normalize_color(tag.substr(value_start, value_end - value_start));
    return !color->empty();
}

static std::vector<text_run_t> parse_inline_runs(const std::string& text)
{
    std::vector<text_run_t> runs;
    std::vector<std::string> color_stack;
    style_t style;
    std::string pending;
    std::size_t index = 0u;

    while (index < text.size()) {
        if (text[index] == '\\' && index + 1u < text.size()) {
            pending += text[index + 1u];
            index += 2u;
        } else if (starts_with_at(text, index, "**") || starts_with_at(text, index, "__")) {
            flush_run(&runs, &pending, style);
            style.bold = !style.bold;
            index += 2u;
        } else if (text[index] == '*' || text[index] == '_') {
            flush_run(&runs, &pending, style);
            style.italic = !style.italic;
            index++;
        } else if (starts_with_at(lower_copy(text.substr(index, 3u)), 0u, "<b>")) {
            flush_run(&runs, &pending, style);
            style.bold = true;
            index += 3u;
        } else if (starts_with_at(lower_copy(text.substr(index, 4u)), 0u, "</b>")) {
            flush_run(&runs, &pending, style);
            style.bold = false;
            index += 4u;
        } else if (starts_with_at(lower_copy(text.substr(index, 8u)), 0u, "<strong>")) {
            flush_run(&runs, &pending, style);
            style.bold = true;
            index += 8u;
        } else if (starts_with_at(lower_copy(text.substr(index, 9u)), 0u, "</strong>")) {
            flush_run(&runs, &pending, style);
            style.bold = false;
            index += 9u;
        } else if (starts_with_at(lower_copy(text.substr(index, 3u)), 0u, "<i>")) {
            flush_run(&runs, &pending, style);
            style.italic = true;
            index += 3u;
        } else if (starts_with_at(lower_copy(text.substr(index, 4u)), 0u, "</i>")) {
            flush_run(&runs, &pending, style);
            style.italic = false;
            index += 4u;
        } else if (starts_with_at(lower_copy(text.substr(index, 4u)), 0u, "<em>")) {
            flush_run(&runs, &pending, style);
            style.italic = true;
            index += 4u;
        } else if (starts_with_at(lower_copy(text.substr(index, 5u)), 0u, "</em>")) {
            flush_run(&runs, &pending, style);
            style.italic = false;
            index += 5u;
        } else if (starts_with_at(lower_copy(text.substr(index, 6u)), 0u, "<span ")) {
            std::size_t tag_end = text.find('>', index);
            std::string color;

            if (tag_end == std::string::npos) {
                pending += text[index];
                index++;
            } else if (extract_span_color(text.substr(index, tag_end - index + 1u), &color)) {
                flush_run(&runs, &pending, style);
                color_stack.push_back(style.color);
                style.color = color;
                index = tag_end + 1u;
            } else {
                pending += text[index];
                index++;
            }
        } else if (starts_with_at(lower_copy(text.substr(index, 7u)), 0u, "</span>")) {
            flush_run(&runs, &pending, style);
            if (!color_stack.empty()) {
                style.color = color_stack.back();
                color_stack.pop_back();
            } else {
                style.color = "#1f2328";
            }
            index += 7u;
        } else if (starts_with_at(lower_copy(text.substr(index, 7u)), 0u, "[color=")) {
            std::size_t tag_end = text.find(']', index);
            std::string color;

            if (tag_end == std::string::npos) {
                pending += text[index];
                index++;
            } else {
                color = normalize_color(text.substr(index + 7u, tag_end - index - 7u));
                if (!color.empty()) {
                    flush_run(&runs, &pending, style);
                    color_stack.push_back(style.color);
                    style.color = color;
                    index = tag_end + 1u;
                } else {
                    pending += text[index];
                    index++;
                }
            }
        } else if (starts_with_at(lower_copy(text.substr(index, 8u)), 0u, "[/color]")) {
            flush_run(&runs, &pending, style);
            if (!color_stack.empty()) {
                style.color = color_stack.back();
                color_stack.pop_back();
            } else {
                style.color = "#1f2328";
            }
            index += 8u;
        } else {
            pending += text[index];
            index++;
        }
    }

    flush_run(&runs, &pending, style);
    if (runs.empty()) {
        text_run_t empty_run;
        empty_run.text = "";
        empty_run.style = style_t();
        runs.push_back(empty_run);
    }

    return runs;
}

static std::size_t plain_length_from_runs(const std::vector<text_run_t>& runs)
{
    std::size_t length = 0u;
    std::size_t index = 0u;

    for (index = 0u; index < runs.size(); index++) {
        length += runs[index].text.size();
    }

    return length;
}

static std::vector<std::string> split_markdown_row(const std::string& line)
{
    std::vector<std::string> cells;
    std::string current;
    std::size_t first = 0u;
    std::size_t last = line.size();
    std::size_t index = 0u;

    while (first < last && std::isspace(static_cast<unsigned char>(line[first])) != 0) {
        first++;
    }
    while (last > first && std::isspace(static_cast<unsigned char>(line[last - 1u])) != 0) {
        last--;
    }
    if (first < last && line[first] == '|') {
        first++;
    }
    if (last > first && line[last - 1u] == '|') {
        last--;
    }

    for (index = first; index < last; index++) {
        if (line[index] == '\\' && index + 1u < last) {
            current += line[index + 1u];
            index++;
        } else if (line[index] == '|') {
            cells.push_back(trim(current));
            current.clear();
        } else {
            current += line[index];
        }
    }

    cells.push_back(trim(current));
    return cells;
}

static bool is_separator_cell(const std::string& value)
{
    std::string cell = trim(value);
    std::size_t index = 0u;
    std::size_t dash_count = 0u;

    if (!cell.empty() && cell[0] == ':') {
        index++;
    }

    while (index < cell.size() && cell[index] == '-') {
        dash_count++;
        index++;
    }

    if (dash_count < 3u) {
        return false;
    }

    if (index < cell.size() && cell[index] == ':') {
        index++;
    }

    while (index < cell.size()) {
        if (std::isspace(static_cast<unsigned char>(cell[index])) == 0) {
            return false;
        }
        index++;
    }

    return true;
}

static bool is_separator_line(const std::string& line)
{
    std::vector<std::string> cells = split_markdown_row(line);
    std::size_t index = 0u;

    if (cells.empty()) {
        return false;
    }

    for (index = 0u; index < cells.size(); index++) {
        if (!is_separator_cell(cells[index])) {
            return false;
        }
    }

    return true;
}

static cell_t make_cell(const std::string& value)
{
    cell_t cell;

    cell.raw = value;
    cell.runs = parse_inline_runs(value);
    cell.plain_length = plain_length_from_runs(cell.runs);
    return cell;
}

static bool parse_table(const std::string& markdown, table_t* table, std::string* error)
{
    std::istringstream stream(markdown);
    std::vector<std::string> lines;
    std::string line;
    std::size_t index = 0u;
    std::size_t start = std::string::npos;

    while (std::getline(stream, line)) {
        if (!line.empty() && line[line.size() - 1u] == '\r') {
            line.erase(line.size() - 1u);
        }
        lines.push_back(line);
    }

    for (index = 0u; index + 1u < lines.size(); index++) {
        if (lines[index].find('|') != std::string::npos && is_separator_line(lines[index + 1u])) {
            start = index;
            break;
        }
    }

    if (start == std::string::npos) {
        *error = "No Markdown table was found. Expected a header row followed by a separator row.";
        return false;
    }

    for (index = start; index < lines.size(); index++) {
        std::vector<std::string> raw_cells;
        row_t row;
        std::size_t cell_index = 0u;

        if (index == start + 1u) {
            continue;
        }
        if (lines[index].find('|') == std::string::npos) {
            break;
        }

        raw_cells = split_markdown_row(lines[index]);
        if (raw_cells.empty()) {
            break;
        }

        for (cell_index = 0u; cell_index < raw_cells.size(); cell_index++) {
            row.cells.push_back(make_cell(raw_cells[cell_index]));
        }

        if (row.cells.size() > table->column_count) {
            table->column_count = row.cells.size();
        }
        table->rows.push_back(row);
    }

    if (table->rows.empty() || table->column_count == 0u) {
        *error = "The Markdown table is empty.";
        return false;
    }

    for (index = 0u; index < table->rows.size(); index++) {
        while (table->rows[index].cells.size() < table->column_count) {
            table->rows[index].cells.push_back(make_cell(""));
        }
    }

    return true;
}

static bool read_file_limited(const std::string& path, std::string* content, std::string* error)
{
    std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
    std::ostringstream buffer;
    std::streamoff size = 0;

    if (!file) {
        *error = "Could not open input file: " + path;
        return false;
    }

    file.seekg(0, std::ios::end);
    size = file.tellg();
    if (size < 0) {
        *error = "Could not determine input file size: " + path;
        return false;
    }
    if (static_cast<std::size_t>(size) > MAX_INPUT_SIZE) {
        *error = "Input file is larger than the supported 4 MiB limit.";
        return false;
    }
    file.seekg(0, std::ios::beg);

    buffer << file.rdbuf();
    if (!file.good() && !file.eof()) {
        *error = "Could not read input file: " + path;
        return false;
    }

    *content = buffer.str();
    return true;
}

static std::string default_svg_path(const std::string& png_path)
{
    std::size_t slash = png_path.find_last_of("/\\");
    std::size_t dot = png_path.find_last_of('.');

    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        return png_path.substr(0u, dot) + ".svg";
    }

    return png_path + ".svg";
}

static int clamped_column_width(std::size_t content_length)
{
    int width = static_cast<int>(content_length * 9u) + (CELL_PADDING_X * 2);

    if (width < MIN_COLUMN_WIDTH) {
        width = MIN_COLUMN_WIDTH;
    }
    if (width > MAX_COLUMN_WIDTH) {
        width = MAX_COLUMN_WIDTH;
    }

    return width;
}

static std::vector<int> compute_column_widths(const table_t& table)
{
    std::vector<int> widths;
    std::size_t column = 0u;
    std::size_t row = 0u;

    for (column = 0u; column < table.column_count; column++) {
        std::size_t max_length = 0u;

        for (row = 0u; row < table.rows.size(); row++) {
            if (table.rows[row].cells[column].plain_length > max_length) {
                max_length = table.rows[row].cells[column].plain_length;
            }
        }

        widths.push_back(clamped_column_width(max_length));
    }

    return widths;
}

static int estimate_run_width(const text_run_t& run)
{
    int width = static_cast<int>(run.text.size() * 8u);

    if (run.style.bold) {
        width += static_cast<int>(run.text.size());
    }

    return width;
}

static bool write_svg(const table_t& table, const std::string& path, int* width_out, int* height_out, std::string* error)
{
    std::vector<int> column_widths = compute_column_widths(table);
    std::ofstream file(path.c_str(), std::ios::out | std::ios::binary);
    int width = 1;
    int height = 1;
    int x = 0;
    int y = 0;
    std::size_t row = 0u;
    std::size_t column = 0u;

    if (!file) {
        *error = "Could not open SVG output file: " + path;
        return false;
    }

    width = 1;
    for (column = 0u; column < column_widths.size(); column++) {
        width += column_widths[column];
    }
    height = 1 + static_cast<int>(table.rows.size()) * ROW_HEIGHT;

    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height
         << "\" viewBox=\"0 0 " << width << " " << height << "\">\n";
    file << "  <rect x=\"0\" y=\"0\" width=\"" << width << "\" height=\"" << height
         << "\" fill=\"#ffffff\" stroke=\"none\"/>\n";

    y = 0;
    for (row = 0u; row < table.rows.size(); row++) {
        x = 0;
        for (column = 0u; column < table.column_count; column++) {
            const cell_t& cell = table.rows[row].cells[column];
            const std::string fill = row == 0u ? "#eef3f8" : (row % 2u == 0u ? "#ffffff" : "#fbfcfd");
            int text_x = x + CELL_PADDING_X;
            const int text_y = y + CELL_PADDING_Y + FONT_SIZE;
            std::size_t run_index = 0u;

            file << "  <rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << column_widths[column]
                 << "\" height=\"" << ROW_HEIGHT << "\" fill=\"" << fill << "\" stroke=\"#c8d0da\"/>\n";

            for (run_index = 0u; run_index < cell.runs.size(); run_index++) {
                text_run_t run = cell.runs[run_index];

                if (run.text.empty()) {
                    continue;
                }
                if (row == 0u) {
                    run.style.bold = true;
                }

                file << "  <text x=\"" << text_x << "\" y=\"" << text_y << "\" font-family=\"Segoe UI, Arial, sans-serif\""
                     << " font-size=\"" << FONT_SIZE << "\" fill=\"" << run.style.color << "\"";
                if (run.style.bold) {
                    file << " font-weight=\"700\"";
                }
                if (run.style.italic) {
                    file << " font-style=\"italic\"";
                }
                file << ">" << xml_escape(run.text) << "</text>\n";
                text_x += estimate_run_width(run);
            }

            x += column_widths[column];
        }
        y += ROW_HEIGHT;
    }

    file << "</svg>\n";
    if (!file) {
        *error = "Could not finish writing SVG output file: " + path;
        return false;
    }

    *width_out = width;
    *height_out = height;
    return true;
}

static std::map<std::string, std::string> parse_attributes(const std::string& tag)
{
    std::map<std::string, std::string> attributes;
    std::size_t index = 0u;

    while (index < tag.size()) {
        std::string name;
        std::string value;

        while (index < tag.size() && std::isspace(static_cast<unsigned char>(tag[index])) == 0) {
            index++;
        }
        while (index < tag.size() && std::isspace(static_cast<unsigned char>(tag[index])) != 0) {
            index++;
        }
        while (index < tag.size()) {
            const char ch = tag[index];
            if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == '=' || ch == '/' || ch == '>') {
                break;
            }
            name += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            index++;
        }
        while (index < tag.size() && std::isspace(static_cast<unsigned char>(tag[index])) != 0) {
            index++;
        }
        if (index >= tag.size() || tag[index] != '=') {
            continue;
        }
        index++;
        while (index < tag.size() && std::isspace(static_cast<unsigned char>(tag[index])) != 0) {
            index++;
        }
        if (index >= tag.size() || tag[index] != '"') {
            continue;
        }
        index++;
        while (index < tag.size() && tag[index] != '"') {
            value += tag[index];
            index++;
        }
        if (index < tag.size() && tag[index] == '"') {
            index++;
        }
        if (!name.empty()) {
            attributes[name] = value;
        }
    }

    return attributes;
}

static int int_attribute(const std::map<std::string, std::string>& attributes, const std::string& name, int fallback)
{
    std::map<std::string, std::string>::const_iterator found = attributes.find(name);

    if (found == attributes.end()) {
        return fallback;
    }

    return std::atoi(found->second.c_str());
}

static std::string string_attribute(const std::map<std::string, std::string>& attributes, const std::string& name, const std::string& fallback)
{
    std::map<std::string, std::string>::const_iterator found = attributes.find(name);

    if (found == attributes.end()) {
        return fallback;
    }

    return found->second;
}

static bool parse_generated_svg(const std::string& svg, int* width, int* height, std::vector<svg_rect_t>* rects, std::vector<svg_text_t>* texts, std::string* error)
{
    std::size_t svg_pos = svg.find("<svg");
    std::size_t svg_end = std::string::npos;
    std::size_t position = 0u;

    if (svg_pos == std::string::npos) {
        *error = "Generated SVG does not contain an <svg> element.";
        return false;
    }

    svg_end = svg.find('>', svg_pos);
    if (svg_end == std::string::npos) {
        *error = "Generated SVG contains a malformed <svg> element.";
        return false;
    }

    {
        std::map<std::string, std::string> attributes = parse_attributes(svg.substr(svg_pos, svg_end - svg_pos + 1u));
        *width = int_attribute(attributes, "width", 0);
        *height = int_attribute(attributes, "height", 0);
    }

    if (*width <= 0 || *height <= 0 || *width > 32000 || *height > 32000) {
        *error = "Generated SVG has invalid image dimensions.";
        return false;
    }

    while ((position = svg.find("<rect", position)) != std::string::npos) {
        std::size_t tag_end = svg.find('>', position);
        std::map<std::string, std::string> attributes;
        svg_rect_t rect;

        if (tag_end == std::string::npos) {
            *error = "Generated SVG contains a malformed <rect> element.";
            return false;
        }

        attributes = parse_attributes(svg.substr(position, tag_end - position + 1u));
        rect.x = int_attribute(attributes, "x", 0);
        rect.y = int_attribute(attributes, "y", 0);
        rect.width = int_attribute(attributes, "width", 0);
        rect.height = int_attribute(attributes, "height", 0);
        rect.fill = string_attribute(attributes, "fill", "#ffffff");
        rect.stroke = string_attribute(attributes, "stroke", "none");
        rects->push_back(rect);
        position = tag_end + 1u;
    }

    position = 0u;
    while ((position = svg.find("<text", position)) != std::string::npos) {
        std::size_t tag_end = svg.find('>', position);
        std::size_t close = std::string::npos;
        std::map<std::string, std::string> attributes;
        svg_text_t text;

        if (tag_end == std::string::npos) {
            *error = "Generated SVG contains a malformed <text> element.";
            return false;
        }

        close = svg.find("</text>", tag_end);
        if (close == std::string::npos) {
            *error = "Generated SVG contains an unclosed <text> element.";
            return false;
        }

        attributes = parse_attributes(svg.substr(position, tag_end - position + 1u));
        text.x = int_attribute(attributes, "x", 0);
        text.y = int_attribute(attributes, "y", 0);
        text.size = int_attribute(attributes, "font-size", FONT_SIZE);
        text.bold = string_attribute(attributes, "font-weight", "") == "700";
        text.italic = string_attribute(attributes, "font-style", "") == "italic";
        text.fill = string_attribute(attributes, "fill", "#1f2328");
        text.text = xml_unescape(svg.substr(tag_end + 1u, close - tag_end - 1u));
        texts->push_back(text);
        position = close + 7u;
    }

    return true;
}

#ifdef _WIN32
static std::wstring utf8_to_wide(const std::string& value)
{
    std::wstring result;
    int required = 0;

    if (value.empty()) {
        return result;
    }

    required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), NULL, 0);
    if (required <= 0) {
        return std::wstring(value.begin(), value.end());
    }

    result.resize(static_cast<std::size_t>(required));
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0], required);
    return result;
}

static Gdiplus::Color gdiplus_color_from_string(const std::string& value)
{
    const std::string color = lower_copy(value);

    if (color.size() == 7u && color[0] == '#') {
        const unsigned int red = static_cast<unsigned int>(std::strtoul(color.substr(1u, 2u).c_str(), NULL, 16));
        const unsigned int green = static_cast<unsigned int>(std::strtoul(color.substr(3u, 2u).c_str(), NULL, 16));
        const unsigned int blue = static_cast<unsigned int>(std::strtoul(color.substr(5u, 2u).c_str(), NULL, 16));
        return Gdiplus::Color(255u, red, green, blue);
    }
    if (color == "black") {
        return Gdiplus::Color(255u, 0u, 0u, 0u);
    }
    if (color == "white") {
        return Gdiplus::Color(255u, 255u, 255u, 255u);
    }
    if (color == "red") {
        return Gdiplus::Color(255u, 210u, 38u, 48u);
    }
    if (color == "green") {
        return Gdiplus::Color(255u, 28u, 127u, 70u);
    }
    if (color == "blue") {
        return Gdiplus::Color(255u, 9u, 105u, 218u);
    }
    if (color == "yellow") {
        return Gdiplus::Color(255u, 154u, 103u, 0u);
    }
    if (color == "purple") {
        return Gdiplus::Color(255u, 130u, 80u, 223u);
    }
    if (color == "orange") {
        return Gdiplus::Color(255u, 188u, 76u, 0u);
    }
    if (color == "gray" || color == "grey") {
        return Gdiplus::Color(255u, 87u, 96u, 106u);
    }
    if (color == "cyan") {
        return Gdiplus::Color(255u, 31u, 147u, 186u);
    }
    if (color == "magenta" || color == "pink") {
        return Gdiplus::Color(255u, 191u, 57u, 137u);
    }
    if (color == "brown") {
        return Gdiplus::Color(255u, 149u, 96u, 58u);
    }
    if (color == "navy") {
        return Gdiplus::Color(255u, 5u, 80u, 174u);
    }
    if (color == "teal") {
        return Gdiplus::Color(255u, 18u, 129u, 133u);
    }

    return Gdiplus::Color(255u, 31u, 35u, 40u);
}

static bool get_png_encoder_clsid(CLSID* encoder)
{
    UINT count = 0u;
    UINT bytes = 0u;
    Gdiplus::ImageCodecInfo* codecs = NULL;
    UINT index = 0u;

    Gdiplus::GetImageEncodersSize(&count, &bytes);
    if (bytes == 0u) {
        return false;
    }

    codecs = static_cast<Gdiplus::ImageCodecInfo*>(std::malloc(bytes));
    if (codecs == NULL) {
        return false;
    }

    Gdiplus::GetImageEncoders(count, bytes, codecs);
    for (index = 0u; index < count; index++) {
        if (wcscmp(codecs[index].MimeType, L"image/png") == 0) {
            *encoder = codecs[index].Clsid;
            std::free(codecs);
            return true;
        }
    }

    std::free(codecs);
    return false;
}

static bool render_svg_to_png(const std::string& svg_path, const std::string& png_path, std::string* error)
{
    std::string svg;
    std::vector<svg_rect_t> rects;
    std::vector<svg_text_t> texts;
    int width = 0;
    int height = 0;
    ULONG_PTR gdiplus_token = 0u;
    Gdiplus::GdiplusStartupInput gdiplus_input;
    Gdiplus::Status status = Gdiplus::Ok;
    CLSID png_encoder;
    std::size_t index = 0u;

    if (!read_file_limited(svg_path, &svg, error)) {
        return false;
    }
    if (!parse_generated_svg(svg, &width, &height, &rects, &texts, error)) {
        return false;
    }

    status = Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, NULL);
    if (status != Gdiplus::Ok) {
        *error = "Could not start GDI+ for PNG rendering.";
        return false;
    }

    {
        Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppARGB);
        Gdiplus::Graphics graphics(&bitmap);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        for (index = 0u; index < rects.size(); index++) {
            Gdiplus::SolidBrush brush(gdiplus_color_from_string(rects[index].fill));
            graphics.FillRectangle(&brush, rects[index].x, rects[index].y, rects[index].width, rects[index].height);
            if (rects[index].stroke != "none") {
                Gdiplus::Pen pen(gdiplus_color_from_string(rects[index].stroke), 1.0f);
                graphics.DrawRectangle(&pen, rects[index].x, rects[index].y, rects[index].width, rects[index].height);
            }
        }

        for (index = 0u; index < texts.size(); index++) {
            INT style = Gdiplus::FontStyleRegular;
            std::wstring wide_text = utf8_to_wide(texts[index].text);
            Gdiplus::StringFormat format;
            Gdiplus::SolidBrush brush(gdiplus_color_from_string(texts[index].fill));
            Gdiplus::RectF layout(
                static_cast<Gdiplus::REAL>(texts[index].x),
                static_cast<Gdiplus::REAL>(texts[index].y - texts[index].size - 2),
                static_cast<Gdiplus::REAL>(width - texts[index].x),
                static_cast<Gdiplus::REAL>(texts[index].size + 8));

            if (texts[index].bold) {
                style |= Gdiplus::FontStyleBold;
            }
            if (texts[index].italic) {
                style |= Gdiplus::FontStyleItalic;
            }

            Gdiplus::Font font(L"Segoe UI", static_cast<Gdiplus::REAL>(texts[index].size), style, Gdiplus::UnitPixel);
            format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip);
            graphics.DrawString(wide_text.c_str(), static_cast<INT>(wide_text.size()), &font, layout, &format, &brush);
        }

        if (!get_png_encoder_clsid(&png_encoder)) {
            *error = "Could not find the PNG encoder.";
            status = Gdiplus::GenericError;
        } else {
            status = bitmap.Save(utf8_to_wide(png_path).c_str(), &png_encoder, NULL);
        }
    }

    Gdiplus::GdiplusShutdown(gdiplus_token);
    if (status != Gdiplus::Ok) {
        if (error->empty()) {
            *error = "Could not save PNG output file: " + png_path;
        }
        return false;
    }

    return true;
}
#else
static bool render_svg_to_png(const std::string& svg_path, const std::string& png_path, std::string* error)
{
    (void)svg_path;
    (void)png_path;
    *error = "PNG rendering is currently implemented with Windows GDI+ and requires a Windows build.";
    return false;
}
#endif

static void print_help()
{
    std::cout
        << "Usage:\n"
        << "  itable <input.md> <output.png> [--svg <output.svg>]\n"
        << "  itable --input <input.md> --output <output.png> [--svg <output.svg>]\n\n"
        << "The first Markdown table is converted to SVG, then the generated SVG is rendered to PNG.\n"
        << "Inline markers: **bold**, *italic*, <b>, <i>, <span style=\"color:#0969da\">, [color=red].\n";
}

static bool parse_arguments(int argc, char** argv, options_t* options, std::string* error)
{
    std::vector<std::string> positional;
    int index = 1;

    while (index < argc) {
        std::string arg = argv[index];

        if (arg == "--help" || arg == "-h") {
            options->show_help = true;
            index++;
        } else if (arg == "--version") {
            options->show_version = true;
            index++;
        } else if (arg == "--input" || arg == "-i") {
            if (index + 1 >= argc) {
                *error = "--input requires a path.";
                return false;
            }
            options->input_path = argv[index + 1];
            index += 2;
        } else if (arg == "--output" || arg == "-o") {
            if (index + 1 >= argc) {
                *error = "--output requires a path.";
                return false;
            }
            options->png_path = argv[index + 1];
            index += 2;
        } else if (arg == "--svg") {
            if (index + 1 >= argc) {
                *error = "--svg requires a path.";
                return false;
            }
            options->svg_path = argv[index + 1];
            index += 2;
        } else if (!arg.empty() && arg[0] == '-') {
            *error = "Unknown option: " + arg;
            return false;
        } else {
            positional.push_back(arg);
            index++;
        }
    }

    if (!positional.empty()) {
        if (options->input_path.empty()) {
            options->input_path = positional[0];
        }
        if (positional.size() > 1u && options->png_path.empty()) {
            options->png_path = positional[1];
        }
        if (positional.size() > 2u) {
            *error = "Too many positional arguments.";
            return false;
        }
    }

    if (options->show_help || options->show_version) {
        return true;
    }

    if (options->input_path.empty()) {
        *error = "Missing input Markdown file.";
        return false;
    }
    if (options->png_path.empty()) {
        *error = "Missing output PNG file.";
        return false;
    }
    if (options->svg_path.empty()) {
        options->svg_path = default_svg_path(options->png_path);
    }

    return true;
}

int main(int argc, char** argv)
{
    options_t options;
    std::string error;
    std::string markdown;
    table_t table;
    int svg_width = 0;
    int svg_height = 0;

    if (!parse_arguments(argc, argv, &options, &error)) {
        std::cerr << "itable: " << error << std::endl;
        std::cerr << "Try 'itable --help' for usage." << std::endl;
        return 2;
    }

    if (options.show_help) {
        print_help();
        return 0;
    }
    if (options.show_version) {
        std::cout << VERSION_TEXT << std::endl;
        return 0;
    }

    if (!read_file_limited(options.input_path, &markdown, &error)) {
        std::cerr << "itable: " << error << std::endl;
        return 1;
    }
    if (!parse_table(markdown, &table, &error)) {
        std::cerr << "itable: " << error << std::endl;
        return 1;
    }
    if (!write_svg(table, options.svg_path, &svg_width, &svg_height, &error)) {
        std::cerr << "itable: " << error << std::endl;
        return 1;
    }
    if (!render_svg_to_png(options.svg_path, options.png_path, &error)) {
        std::cerr << "itable: " << error << std::endl;
        return 1;
    }

    std::cout << "Wrote " << options.png_path << " (" << svg_width << "x" << svg_height << ")" << std::endl;
    std::cout << "Wrote " << options.svg_path << std::endl;
    return 0;
}
