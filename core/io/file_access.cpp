#include "file_access.h"
#include "os/memory.h"
#include "path.h"

FileAccess::~FileAccess()
{
    close();
}

FileAccess *FileAccess::open(const std::string path, ModeFlag flag)
{
    const std::string absolute_path = Path::replace_url(path);
    const char *mode = "rb";
    switch (flag)
    {
    case ModeFlag::READ: mode = "rb"; break;
    case ModeFlag::WRITE: mode = "wb"; break;
    case ModeFlag::READ_WRITE: mode = "r+b"; break;
    }

    FILE *file = fopen(absolute_path.c_str(), mode);
    if (file == nullptr)
    {
        return nullptr;
    }

    FileAccess *file_access = memnew(FileAccess);
    file_access->file = file;
    return file_access;
}

std::string FileAccess::get_file_as_text(const std::string path)
{
    FileAccess *file_access = open(path, ModeFlag::READ);
    if (file_access == nullptr)
    {
        return std::string();
    }

    const std::string text = file_access->get_as_text();
    memdelete(file_access);
    return text;
}

bool FileAccess::file_exists(const std::string path)
{
    const std::string absolute_path = Path::replace_url(path);
    FILE *file = fopen(absolute_path.c_str(), "rb");
    if (file != nullptr)
    {
        fclose(file);
        return true;
    }
    return false;
}

void FileAccess::close()
{
    if (file != nullptr)
    {
        fclose(file);
        file = nullptr;
    }
    cursor = 0;
}

void FileAccess::flush()
{
    if (file != nullptr) fflush(file);
}

bool FileAccess::is_eof() const
{
    return file == nullptr || feof(file) != 0;
}

bool FileAccess::is_open() const
{
    return file != nullptr;
}

size_t FileAccess::get_position() const
{
    if (file == nullptr) return 0;
    const long position = ftell(file);
    if (position < 0) return 0;
    return static_cast<size_t>(position);
}

void FileAccess::seek(size_t position)
{
    if (file == nullptr) return;
    if (fseek(file, static_cast<long>(position), SEEK_SET) == 0)
    {
        cursor = position;
    }
}

void FileAccess::seek_end()
{
    if (file == nullptr) return;
    if (fseek(file, 0, SEEK_END) == 0)
    {
        const long position = ftell(file);
        cursor = position < 0 ? 0 : static_cast<size_t>(position);
    }
}

std::string FileAccess::get_as_text() const
{
    if (file == nullptr) return std::string();

    const long start = ftell(file);
    if (start < 0) return std::string();

    if (fseek(file, 0, SEEK_END) != 0) return std::string();
    const long end = ftell(file);
    if (end < 0) return std::string();

    const long length = end - start;
    if (length <= 0)
    {
        fseek(file, start, SEEK_SET);
        return std::string();
    }

    if (fseek(file, start, SEEK_SET) != 0) return std::string();

    std::string text;
    text.resize(static_cast<size_t>(length));
    const size_t read_bytes = fread(&text[0], 1, static_cast<size_t>(length), file);
    text.resize(read_bytes);
    return text;
}

std::string FileAccess::read_line()
{
    if (file == nullptr) return std::string();

    std::string line;
    int c = EOF;
    while ((c = fgetc(file)) != EOF)
    {
        if (c == '\n') break;
        line.push_back(static_cast<char>(c));
    }

    // 兼容 Windows 的 \r\n 换行
    if (!line.empty() && line.back() == '\r')
    {
        line.pop_back();
    }

    // 游标已移动到换行符之后（或文件末尾）
    cursor = get_position();
    return line;
}
