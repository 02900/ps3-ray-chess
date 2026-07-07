#ifndef RAY_CHESS_COMPAT_H
#define RAY_CHESS_COMPAT_H

#include <string>
#include <stdio.h>

// The PS3 toolchain's newlib-backed libstdc++ omits std::to_string / std::stoi
// (_GLIBCXX_USE_C99 is not defined for this target), so ray-chess's std::to_string
// calls don't compile. This provides the minimal integer formatter the game needs.
// (newlib declares snprintf in the global namespace, not std::.)
namespace compat {
    inline std::string to_string(int value) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", value);
        return std::string(buf);
    }
}

#endif // RAY_CHESS_COMPAT_H
