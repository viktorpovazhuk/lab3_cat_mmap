// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "options_parser.h"
#include "errors.h"

#include <iostream>
#include <ios>
#include <memory>
#include <algorithm>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstddef>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sys/mman.h>
#include <cmath>

using uniq_char_ptr = std::unique_ptr<char[]>;

size_t convert_invisible_chars(uniq_char_ptr &buf_ptr, size_t buf_size, uniq_char_ptr &inv_chars_buf) {
    size_t len_with_repr = 0;
    for (size_t i = 0; i < buf_size; i++) {
        char cur_char = buf_ptr[i];
        if (!std::isprint(cur_char) && !std::isspace(cur_char)) {
            std::stringstream ss;
            ss << "\\x" << std::hex << std::uppercase << std::setw(2)
               << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(cur_char));
            std::string code = ss.str();
            memcpy(&inv_chars_buf[len_with_repr], code.c_str(), code.size());
            len_with_repr += code.size();
        } else {
            inv_chars_buf[len_with_repr] = cur_char;
            len_with_repr++;
        }
    }
    return len_with_repr;
}

size_t write_file(int in_fd, int out_fd, size_t out_size, bool print_inv) {
    size_t page_size = sysconf(_SC_PAGE_SIZE);
    const size_t buf_capacity = page_size * 100;
    const size_t exp_buf_capacity = buf_capacity * 4;
    auto buf_ptr = std::make_unique<char[]>(buf_capacity);
    auto exp_buf_ptr = std::make_unique<char[]>(exp_buf_capacity);

    struct stat stat_buf{};
    fstat(in_fd, &stat_buf);
    size_t in_size = stat_buf.st_size;

    size_t exp_in_size;
    if (print_inv) {
        exp_in_size = in_size * 4;
    }
    else {
        exp_in_size = in_size;
    }
    ftruncate(out_fd, out_size + exp_in_size);

    size_t out_off = out_size;
    size_t in_off = 0;
    void *in_addr, *out_addr;
    while (in_off < in_size) {
        size_t paged_off = ceil(out_off / page_size) * page_size;
        size_t paged_rem = out_off % page_size;
        size_t buf_size = buf_capacity;
        size_t rest_size = in_size - in_off;
        if (buf_size > rest_size) {
            buf_size = rest_size;
        }

        in_addr = mmap(nullptr, buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, in_fd, in_off);
        if (in_addr == MAP_FAILED) {
            perror("input mmap");
        }
        memcpy(buf_ptr.get(), in_addr, buf_size);

        size_t exp_buf_size;
        if (print_inv) {
            exp_buf_size = convert_invisible_chars(buf_ptr, buf_size, exp_buf_ptr);
        }
        else {
            exp_buf_size = buf_size;
            memcpy(exp_buf_ptr.get(), buf_ptr.get(), buf_size);
        }
        out_addr = mmap(nullptr, paged_rem + exp_buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, out_fd, paged_off);
        if (out_addr == MAP_FAILED) {
            perror("output mmap");
        }
        memcpy(static_cast<char *>(out_addr) + paged_rem, exp_buf_ptr.get(), exp_buf_size);
        msync(out_addr, paged_rem + exp_buf_size, MS_SYNC);

        munmap(in_addr, buf_size);
        munmap(out_addr, buf_size);

        in_off += buf_size;
        out_off += exp_buf_size;
    }
    size_t new_size = out_off;
    ftruncate(out_fd, new_size);

    return new_size;
}

int main(int argc, char *argv[]) {
    std::unique_ptr<command_line_options_t> command_line_options;
    try {
        command_line_options = std::make_unique<command_line_options_t>(argc, argv);
    }
    catch (std::exception &ex) {
        std::cerr << ex.what() << std::endl;
        exit(Errors::ECLOPTIONS);
    }
    std::vector<std::string> paths = command_line_options->files;
    bool print_inv = command_line_options->print_trans;
    std::string out_path = command_line_options->out_file;

    std::vector<int> in_fds(paths.size());
    for (size_t i = 0; i < paths.size(); i++) {
        std::string &cur_path = paths[i];
        int cur_fd = open(cur_path.c_str(), O_RDWR);
        if (cur_fd == -1) {
            perror(("Error while opening file: " + cur_path).c_str());
            exit(errno);
        }
        in_fds[i] = cur_fd;
    }
    int out_fd = open(out_path.c_str(), O_RDWR);
    if (out_fd == -1) {
        perror(("Error while opening file: " + out_path).c_str());
        exit(errno);
    }

    ftruncate(out_fd, 0);
    size_t out_size = 0;
    for (int in_fd: in_fds) {
        out_size = write_file(in_fd, out_fd, out_size, print_inv);
    }

    for (int fd: in_fds) {
        int res = close(fd);
        if (res == -1) {
            perror("Error while closing file");
            exit(errno);
        }
    }

    return 0;
}

// time ./mycat ../data/extra_large_1.txt > out.txt