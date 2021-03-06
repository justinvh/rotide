#include <sstream>
#include <iostream>
#include <thread>
#include <chrono>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <codecvt>

#include <rotide/sys/terminal.hpp>

namespace ro {

Terminal::Terminal()
{
    delay = 0;
    activate();
}

Cursor Terminal::cursor(int row, int col)
{
    write(TerminalCode::cursor_set_position, row, col);
    return cursor();
}

void Terminal::cursor_forward(int cols)
{
    write(TerminalCode::cursor_forward, cols);
}

void Terminal::cursor_back(int cols)
{
    write(TerminalCode::cursor_back, cols);
}

void Terminal::cursor_up(int rows)
{
    write(TerminalCode::cursor_up, rows);
}

void Terminal::cursor_down(int rows)
{
    write(TerminalCode::cursor_down, rows);
}

Cursor Terminal::cursor()
{
    int retval, result, rows, cols, saved_errno;

    if (tty == -1) {
        return Cursor(-1, -1);
    }

    termios temporary = push_state();

    do {

        if (!apply_termios(temporary)) {
            break;
        }

        // Request cursor coordinates from the terminal.
        retval = write(TerminalCode::cursor_get_position);
        if (retval) {
            break;
        }

        // Assume coordinate reponse parsing fails.
        retval = EIO;

        // Expect an ESC.
        result = read();
        if (result != 27) {
            break;
        }

        // Expect [ after the ESC.
        result = read();
        if (result != '[') {
            break;
        }

        // Parse rows.
        rows = 0;
        result = read();
        while (result >= '0' && result <= '9') {
            rows = 10 * rows + result - '0';
            result = read();
        }

        if (result != ';') {
            break;
        }

        // Parse cols.
        cols = 0;
        result = read();
        while (result >= '0' && result <= '9') {
            cols = 10 * cols + result - '0';
            result = read();
        }

        if (result != 'R') {
            break;
        }

        retval = 0;

    } while (false);

    temporary = pop_state();
    apply_termios(temporary);

    return Cursor(rows, cols);
}

int Terminal::read()
{
    unsigned char buffer[4];
    ssize_t n;

    while (1) {
        n = ::read(tty, buffer, 1);
        if (n > (ssize_t) 0) {
            return buffer[0];
        } else if (n == (ssize_t) 0) {
            return rd_eof;
        } else if (n != (ssize_t) -1) {
            return rd_eio;
        } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            return rd_eio;
        }
    }
}

Unicode Terminal::read_wchar()
{
    Unicode bytes;
    static unsigned short mask[] = {192, 224, 240};

    unsigned short i, j;

    bytes[0] = read();
    if (bytes[0] == EOF) {
        return bytes;
    }

    i = 0;
    if ((bytes[0] & mask[0]) == mask[0]) i++;
    if ((bytes[0] & mask[1]) == mask[1]) i++;
    if ((bytes[0] & mask[2]) == mask[2]) i++;

    j = 0;
    while (j < i) {
        j++;
        bytes[j] = read();
    }

    return bytes;
}

int Terminal::write(TerminalCode code)
{
    if (code == TerminalCode::reset) {
        return write("\033c");
    } else if (code == TerminalCode::cursor_get_position) {
        return write("\033[6n");
    } else {
        return -1;
    }
}

int Terminal::write(TerminalCode code, int arg1)
{
    std::stringstream ss;

    ss << "\033[" << arg1;

    if (code == TerminalCode::cursor_up) {
        ss << "A";
    } else if (code == TerminalCode::cursor_down) {
        ss << "B";
    } else if (code == TerminalCode::cursor_forward) {
        ss << "C";
    } else if (code == TerminalCode::cursor_back) {
        ss << "D";
    } else if (code == TerminalCode::cursor_next_line) {
        ss << "E";
    } else if (code == TerminalCode::cursor_prev_line) {
        ss << "F";
    } else if (code == TerminalCode::cursor_set_col) {
        ss << "F";
    } else {
        return -1;
    }

    return write(ss.str());
}

int Terminal::write(TerminalCode code, int arg1, int arg2)
{
    std::stringstream ss;

    ss << "\033[";

    if (code == TerminalCode::cursor_set_position) {
        ss << arg1 << ";" << arg2 << "H";
    } else {
        return -1;
    }

    return write(ss.str());
}

int Terminal::write(char k)
{
    int n = ::write(tty, &k, 1);

    if (delay) {
        std::chrono::milliseconds dura(delay);
        std::this_thread::sleep_for(dura);
    }

    return n;
}

int Terminal::write(const std::string& data)
{
    const char* head = data.c_str();
    const char* const tail = head + data.size();
    ssize_t n;

    while (head < tail) {
        n = ::write(tty, head, (size_t)(tail - head));
        if (n > (ssize_t) 0) {
            head += n;
        } else if (n != (ssize_t) -1) {
            return EIO;
        } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            return errno;
        }
    }

    if (delay) {
        std::chrono::milliseconds dura(delay);
        std::this_thread::sleep_for(dura);
    }

    return 0;
}

int Terminal::write(const Unicode& unicode)
{
    const char* head = unicode.data;
    const char* const tail = head + 4;
    ssize_t n;

    while (head < tail) {
        n = ::write(tty, head, (size_t)(tail - head));
        if (n > (ssize_t) 0) {
            head += n;
        } else if (n != (ssize_t) -1) {
            return EIO;
        } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            return errno;
        }
    }

    if (delay) {
        std::chrono::milliseconds dura(delay);
        std::this_thread::sleep_for(dura);
    }

    return 0;
}

bool Terminal::activate()
{
    const char* dev;

    dev = ttyname(STDIN_FILENO);
    if (!dev) {
        dev = ttyname(STDOUT_FILENO);
    }

    if (!dev) {
        dev = ttyname(STDERR_FILENO);
    }

    if (!dev) {
        errno = ENOTTY;
        return false;
    }

    do {
        tty = open(dev, O_RDWR | O_NOCTTY);
    } while (tty == -1 && errno == EINTR);

    return tty != -1;
}

void Terminal::reset()
{
    write(TerminalCode::reset);
}

bool Terminal::apply_termios(termios state)
{
    int result;

    do {
        result = tcsetattr(tty, TCSANOW, &state);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
        return false;
    }

    return true;
}

termios Terminal::push_state()
{
    int result, saved_errno = errno;
    termios saved, temporary;

    // Save current terminal settings.
    do {
        result = tcgetattr(tty, &saved);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
        std::cerr << "Failed to save current terminal settings" << std::endl;
        errno = saved_errno;
        return saved;
    }

    term_stack.push_back(saved);

    // Get current terminal settings for basis, too.
    do {
        result = tcgetattr(tty, &temporary);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
        std::cerr << "Failed to save current terminal settings" << std::endl;
        errno = saved_errno;
        return saved;
    }

    // Disable ICANON, ECHO, and CREAD.
    temporary.c_lflag &= ~ICANON;
    temporary.c_lflag &= ~ECHO;
    temporary.c_cflag &= ~CREAD;

    return temporary;
}

termios Terminal::pop_state()
{
    termios state = term_stack.back();
    term_stack.pop_back();
    return state;
}

Unicode Terminal::getch()
{
    apply_termios(push_state());
    auto data = read_wchar();
    apply_termios(pop_state());
    return data;
}

}
